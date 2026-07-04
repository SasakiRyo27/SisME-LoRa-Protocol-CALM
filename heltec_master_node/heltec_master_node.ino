/*
 * SisME - Sistema de Mensajería de Emergencia
 * Copyright (C) 2026  Massimo Larger & Claudio Uribe
 *
 * Este programa es software libre: puedes redistribuirlo y/o modificarlo
 * bajo los términos de la Licencia Pública General de GNU publicada por
 * la Free Software Foundation, ya sea la versión 3 de la Licencia, o
 * (a tu elección) cualquier versión posterior.
 *
 * Este programa se distribuye con la esperanza de que sea útil.
 * Consulte la Licencia Pública General de GNU para obtener más detalles.
 */

#include <heltec_unofficial.h>
#include <mbedtls/aes.h>
#include <map>
#include <vector>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============ CONFIGURACIÓN WiFi ============
//const char* wifi_ssid = "X";        // Red WiFi local
//const char* wifi_password = "X";     // Contraseña

// Configuración del servidor Django
//const char* django_server = "http://x.x.x.x:8000"; <--- Reemplazar con la IP del servidor Django
const char* api_send_message = "/api/send-message/";
const char* api_get_messages = "/api/get-messages/";
const char* api_update_node = "/api/update-node/";
const char* api_sync_user = "/api/sync-user/";
const char* api_users_index = "/api/users-index/";
const char* api_user_export = "/api/user-export/";
const char* api_update_user_location = "/api/update-user-location/";

// ============ CONFIGURACIÓN LoRa ============
#define FREQUENCY 915.0

const uint8_t AES_KEY[16] = {
  0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
  0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

#define DISPOSITIVO_ID 4
#define ZONA_NODO "Zona Sur"

#define SEPARADOR ':'
#define SEPARADOR_CRC '|'
#define SEPARADOR_ENCRIPTADO '#'
#define SEPARADOR_RUTA '>'

#define TIPO_NORMAL 'N'
#define TIPO_REDIRECCION 'R'
#define TIPO_ACK 'A'
#define TIPO_BEACON 'B'
#define TIPO_RESPUESTA_BEACON 'E'
#define TIPO_BUSQUEDA_USUARIO 'Q'
#define TIPO_NOTIFICACION_USUARIO 'U'

#define MAX_HOPS 10
#define VECINOS_MAX 5
#define TIMEOUT_VECINO 30000
#define TIEMPO_RETENCION_HASH 60000
#define TIMEOUT_MENSAJES_PENDIENTES 10000
#define MIN_INTERVALO_UPDATE_VECINO 100

#define INTERVALO_BEACON 10000
#define INTERVALO_LIMPIEZA_VECINOS 15000
#define INTERVALO_SINCRONIZACION_API 3000
#define INTERVALO_REPORTE_NODO 15000
#define INTERVALO_INTENTO_WIFI 30000
#define TIMEOUT_USER_CREATE_CHUNKS 60000

// ============ ESTRUCTURAS ============
struct Vecino {
  int id;
  int rssi;
  unsigned long ultimoBeacon;
  bool activo;
};

struct MensajeEnRuta {
  String contenido;
  int origen;
  int destino;
  int ttl;
  String ruta;
  unsigned long timestamp;
};

struct MensajeBroadcastVisto {
  String hash;
  String contenido;
  unsigned long timestamp;
};

struct MensajePendienteAPI {
  int origen;
  String remitente;
  String contenido;
  int destino_id;
  int destino_sub_id;
};

struct UsuarioLocal {
  String username;
  int sub_id;
  unsigned long ultimoLogin;
};

struct UsuarioMalla {
  int node_id;
  int sub_id;
  unsigned long last_seen;
};

// ============ VARIABLES GLOBALES ============
std::map<int, Vecino> vecinos;
std::map<String, MensajeEnRuta> mensajesPendientes;
std::vector<MensajeBroadcastVisto> mensajesBroadcastVistos;
std::vector<MensajePendienteAPI> colaMensajesAPI;

const size_t MAX_COLA_API = 50;
std::map<String, UsuarioLocal> usuariosLocales;
std::map<String, std::vector<String>> userCreateChunks;
std::map<String, int> userCreateChunksTotal;
std::map<String, unsigned long> userCreateChunksTs;

std::map<String, std::vector<String>> userListChunks;
std::map<String, int> userListChunksTotal;
std::map<String, unsigned long> userListChunksTs;
std::map<String, int> userListResponder;
std::map<String, UsuarioMalla> tablaRuteoUsuarios;

bool wifiConnected = false;
unsigned long ultimoIntentoWiFi = 0;
unsigned long ultimoBeaconEnviado = 0;
unsigned long ultimaLimpiezaVecinos = 0;
unsigned long ultimaSincronizacionAPI = 0;
unsigned long ultimoReporteNodo = 0;

// Contadores de diagnóstico
int contadorMensajes = 0;
int contadorRedirecciones = 0;
int contadorIgnorados = 0;
int contadorCRCErroneos = 0;

bool mostrandoMensajeTemporal = false;
unsigned long tiempoInicioMensajePantalla = 0;
const unsigned long DURACION_MENSAJE_PANTALLA = 4000;

struct MensajeChat {
  String texto;
  String remitente;
  int idOrigen;
  unsigned long timestamp;
};
MensajeChat historialChat[20];
int totalMensajesHistorial = 0;

String mensajeRecibido = "";
bool mensajeNuevo = false;
unsigned long ultimoEnvioTime = 0;

int contadorBroadcastIgnorados = 0;

// ============ FUNCIÓN BATERÍA ============
int getBatteryLevel() {
  int battery = heltec_battery_percent();
  if (battery <= 0 || battery < 5) return 100;
  return battery > 100 ? 100 : battery;
}

// ============ FUNCIONES DE ENCRIPTACIÓN ============
int pkcs7_padding(uint8_t* data, int data_len, int block_size) {
  int padding_len = block_size - (data_len % block_size);
  for (int i = 0; i < padding_len; i++) data[data_len + i] = padding_len;
  return data_len + padding_len;
}

int pkcs7_unpadding(uint8_t* data, int data_len) {
  if (data_len == 0) return 0;
  int padding_len = data[data_len - 1];
  if (padding_len > 16 || padding_len > data_len) return -1;
  for (int i = 0; i < padding_len; i++) {
    if (data[data_len - 1 - i] != padding_len) return -1;
  }
  return data_len - padding_len;
}

String encriptarAES(const String& textoPlano) {
  if (textoPlano.length() == 0) return "";
  int input_len = textoPlano.length();
  uint8_t input[256] = {0};
  memcpy(input, textoPlano.c_str(), input_len);
  int padded_len = pkcs7_padding(input, input_len, 16);
  uint8_t output[256] = {0};
  mbedtls_aes_context aes_ctx;
  mbedtls_aes_init(&aes_ctx);
  mbedtls_aes_setkey_enc(&aes_ctx, AES_KEY, 128);
  for (int i = 0; i < padded_len; i += 16) {
    mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_ENCRYPT, input + i, output + i);
  }
  mbedtls_aes_free(&aes_ctx);
  String resultado = "";
  for (int i = 0; i < padded_len; i++) {
    char hex[3];
    snprintf(hex, sizeof(hex), "%02X", output[i]);
    resultado += hex;
  }
  return resultado;
}

String desencriptarAES(const String& textoEncriptadoHex) {
  if (textoEncriptadoHex.length() == 0 || textoEncriptadoHex.length() % 2 != 0) return "";
  int encrypted_len = textoEncriptadoHex.length() / 2;
  uint8_t encrypted[256] = {0};
  for (int i = 0; i < encrypted_len; i++) {
    String byteStr = textoEncriptadoHex.substring(i * 2, i * 2 + 2);
    encrypted[i] = (uint8_t)strtol(byteStr.c_str(), NULL, 16);
  }
  uint8_t decrypted[256] = {0};
  mbedtls_aes_context aes_ctx;
  mbedtls_aes_init(&aes_ctx);
  mbedtls_aes_setkey_dec(&aes_ctx, AES_KEY, 128);
  for (int i = 0; i < encrypted_len; i += 16) {
    mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_DECRYPT, encrypted + i, decrypted + i);
  }
  mbedtls_aes_free(&aes_ctx);
  int original_len = pkcs7_unpadding(decrypted, encrypted_len);
  if (original_len < 0) return "";
  decrypted[original_len] = '\0';
  return String((char*)decrypted);
}

// ============ FUNCIONES CRC ============
const uint16_t crc16_table[256] = {
  0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
  0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
  0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
  0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
  0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
  0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
  0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
  0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
  0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
  0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
  0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
  0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
  0x2800, 0xD8C1, 0xD981, 0x1940, 0xDB01, 0x1BC0, 0x1A80, 0xDA41,
  0xDE01, 0x1EC0, 0x1F80, 0xDF41, 0x1D00, 0xDDC1, 0xDC81, 0x1C40,
  0xD401, 0x14C0, 0x1580, 0xD541, 0x1700, 0xD7C1, 0xD681, 0x1640,
  0x1200, 0xD2C1, 0xD381, 0x1340, 0xD101, 0x11C0, 0x1080, 0xD041,
  0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
  0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
  0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
  0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
  0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
  0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
  0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
  0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
  0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
  0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
  0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
  0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
  0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
  0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
  0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
  0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

uint16_t calcularCRC(const String& data) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < data.length(); i++) {
    crc = (crc >> 8) ^ crc16_table[(crc ^ data[i]) & 0xFF];
  }
  return crc;
}

String quitarCRC(String mensaje) {
  int ultimoSeparador = mensaje.lastIndexOf(SEPARADOR_CRC);
  if (ultimoSeparador > 0) return mensaje.substring(0, ultimoSeparador);
  return mensaje;
}

bool verificarCRC(String mensaje) {
  uint16_t crcRecibido;
  int ultimoSeparador = mensaje.lastIndexOf(SEPARADOR_CRC);
  if (ultimoSeparador > 0) {
    String crcStr = mensaje.substring(ultimoSeparador + 1);
    crcStr.trim();
    if (crcStr.length() == 4) {
      crcRecibido = (uint16_t)strtol(crcStr.c_str(), NULL, 16);
      String mensajeSinCRC = mensaje.substring(0, ultimoSeparador);
      uint16_t crcCalculado = calcularCRC(mensajeSinCRC);
      return (crcRecibido == crcCalculado);
    }
  }
  return false;
}

void registrarUsuarioLocal(String username, int sub_id) {
  UsuarioLocal u = {username, sub_id, millis()};
  usuariosLocales[username] = u;
  Serial.printf("[UNICAST] Usuario local registrado: %s (%d.%d)\n", username.c_str(), DISPOSITIVO_ID, sub_id);
}

void buscarUsuarioEnMalla(String username) {
  Serial.printf("[UNICAST] Buscando usuario %s en la malla...\n", username.c_str());
  enviarMensajeLoRa(formatearMensaje(TIPO_BUSQUEDA_USUARIO, 0, DISPOSITIVO_ID, MAX_HOPS, "", username));
}

void notificarCambioUsuario(String username, int nuevoSubId) {
  String contenido = username + ":" + String(DISPOSITIVO_ID) + "." + String(nuevoSubId);
  enviarMensajeLoRa(formatearMensaje(TIPO_NOTIFICACION_USUARIO, 0, DISPOSITIVO_ID, MAX_HOPS, "", contenido));
}

void notificarUbicacionUsuario(String username, int nodeId, int subId) {
  String contenido = username + ":" + String(nodeId) + "." + String(subId);
  enviarMensajeLoRa(formatearMensaje(TIPO_NOTIFICACION_USUARIO, 0, DISPOSITIVO_ID, MAX_HOPS, "", contenido));
}

// ============ FUNCIONES DE GESTIÓN DE VECINOS ============
void limpiarVecinosInactivos() {
  unsigned long ahora = millis();
  for (auto it = vecinos.begin(); it != vecinos.end();) {
    if (ahora - it->second.ultimoBeacon >= TIMEOUT_VECINO) {
      Serial.printf("[VECINO] Eliminando vecino inactivo ID%d\n", it->first);
      it = vecinos.erase(it);
    } else {
      ++it;
    }
  }
}

void actualizarVecino(int id, int rssi) {
  if (id == DISPOSITIVO_ID) return;
  
  unsigned long ahora = millis();
  auto it = vecinos.find(id);
  
  if (it != vecinos.end()) {
    if (ahora - it->second.ultimoBeacon < MIN_INTERVALO_UPDATE_VECINO) return;
    it->second.rssi = rssi;
    it->second.ultimoBeacon = ahora;
    it->second.activo = true;
  } else if (vecinos.size() < VECINOS_MAX) {
    Vecino nuevo = {id, rssi, ahora, true};
    vecinos[id] = nuevo;
    Serial.printf("[VECINO] Nuevo vecino ID%d, RSSI: %d\n", id, rssi);
  } else {
    // Reemplazar el vecino con peor RSSI si el nuevo es mejor
    int peorRssi = rssi;
    int peorId = -1;
    for (auto& par : vecinos) {
      if (par.second.rssi < peorRssi) {
        peorRssi = par.second.rssi;
        peorId = par.first;
      }
    }
    if (peorId != -1) {
      Serial.printf("[VECINO] Reemplazando ID%d (RSSI:%d) por ID%d (RSSI:%d)\n", peorId, peorRssi, id, rssi);
      vecinos.erase(peorId);
      Vecino nuevo = {id, rssi, ahora, true};
      vecinos[id] = nuevo;
    }
  }
}

int encontrarMejorVecino(int destino, int excluirId = -1) {
  int mejorId = -1;
  int mejorRssi = -1000;
  
  for (auto& par : vecinos) {
    if (par.first == excluirId) continue;
    if (par.first == destino) return par.first;
    if (par.second.rssi > mejorRssi) {
      mejorRssi = par.second.rssi;
      mejorId = par.first;
    }
  }
  return mejorId;
}

// ============ DETECCIÓN DE DUPLICADOS ============
String generarHashMensaje(int origen, const String& contenido) {
  uint16_t hash = calcularCRC(String(origen) + ":" + contenido);
  char hashStr[9];
  snprintf(hashStr, sizeof(hashStr), "%04X_%lu", hash, millis() / 1000);
  return String(hashStr);
}

bool esMensajeBroadcastDuplicado(int origen, const String& contenido) {
  unsigned long ahora = millis();
  
  for (auto it = mensajesBroadcastVistos.begin(); it != mensajesBroadcastVistos.end();) {
    if (ahora - it->timestamp > TIEMPO_RETENCION_HASH) {
      it = mensajesBroadcastVistos.erase(it);
    } else {
      ++it;
    }
  }
  
  for (const auto& visto : mensajesBroadcastVistos) {
    if (visto.contenido == contenido && (ahora - visto.timestamp) < 5000) {
      return true;
    }
  }
  
  mensajesBroadcastVistos.push_back({generarHashMensaje(origen, contenido), contenido, ahora});
  return false;
}

bool esMensajeUnicastDuplicado(int origen, int destino, const String& contenido) {
  String key = String(origen) + ":" + String(destino);
  auto it = mensajesPendientes.find(key);
  if (it != mensajesPendientes.end()) {
    if (millis() - it->second.timestamp < 5000 && it->second.contenido == contenido) {
      return true;
    }
  }
  return false;
}

// ============ FUNCIONES DE PANTALLA ============
void agregarAlHistorial(String remitente, String mensaje, int idOrigen) {
  if (totalMensajesHistorial >= 20) {
    for (int i = 0; i < 19; i++) historialChat[i] = historialChat[i + 1];
    totalMensajesHistorial = 19;
  }
  historialChat[totalMensajesHistorial].texto = mensaje;
  historialChat[totalMensajesHistorial].remitente = remitente;
  historialChat[totalMensajesHistorial].idOrigen = idOrigen;
  historialChat[totalMensajesHistorial].timestamp = millis();
  totalMensajesHistorial++;
}

void actualizarPantallaChat() {
  display.clear();
  // Línea superior: Título y Batería
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "SisME LoRa");
  display.drawString(85, 0, String(getBatteryLevel()) + "%");
  
  // Línea 2: Status Red y ID
  String status = wifiConnected ? "WIFI" : "LoRa";
  display.drawString(0, 11, "ID:" + String(DISPOSITIVO_ID) + " [" + status + "]");
  display.drawString(85, 11, "V:" + String(vecinos.size()));
  
  display.drawLine(0, 23, 128, 23);
  
  // Historial de mensajes (3 líneas si son cortos)
  int yOffset = 25;
  int inicio = max(0, totalMensajesHistorial - 3);
  for (int i = inicio; i < totalMensajesHistorial; i++) {
    String msg = historialChat[i].remitente + ": " + historialChat[i].texto;
    if (msg.length() > 22) msg = msg.substring(0, 19) + "...";
    display.drawString(0, yOffset, msg);
    yOffset += 11;
  }
  display.display();
}

void mostrarMensajePantallaNoBlocking(String remitente, String mensaje) {
  display.clear();
  display.drawString(0, 0, remitente);
  display.drawString(0, 16, mensaje.substring(0, 21));
  if (mensaje.length() > 21) display.drawString(0, 28, mensaje.substring(21, 42));
  display.drawString(0, 50, "Bat: " + String(getBatteryLevel()) + "%");
  display.display();
  mostrandoMensajeTemporal = true;
  tiempoInicioMensajePantalla = millis();
}

// ============ FUNCIONES DE MENSAJES LoRa ============
String formatearMensaje(char tipo, int destino, int origen, int ttl, const String& ruta, const String& contenido) {
  String mensajeBase = String(tipo) + SEPARADOR + String(origen) + SEPARADOR + String(destino) + SEPARADOR + String(ttl) + SEPARADOR + ruta + SEPARADOR + contenido;
  
  String mensajeFinal = (tipo == TIPO_BEACON || tipo == TIPO_RESPUESTA_BEACON) ? 
                         mensajeBase : String(SEPARADOR_ENCRIPTADO) + encriptarAES(mensajeBase);
  
  uint16_t crc = calcularCRC(mensajeFinal);
  char crcStr[5];
  snprintf(crcStr, sizeof(crcStr), "%04X", crc);
  return mensajeFinal + SEPARADOR_CRC + String(crcStr);
}

bool parsearMensaje(String mensajeCompleto, char& tipo, int& origen, int& destino, int& ttl, String& ruta, String& contenido) {
  if (!verificarCRC(mensajeCompleto)) return false;
  
  String mensajeSinCRC = quitarCRC(mensajeCompleto);
  bool encriptado = (mensajeSinCRC.length() > 0 && mensajeSinCRC[0] == SEPARADOR_ENCRIPTADO);
  
  String mensajeDecodificado = encriptado ? desencriptarAES(mensajeSinCRC.substring(1)) : mensajeSinCRC;
  if (encriptado && mensajeDecodificado.length() == 0) return false;
  
  int idx1 = mensajeDecodificado.indexOf(SEPARADOR);
  int idx2 = mensajeDecodificado.indexOf(SEPARADOR, idx1 + 1);
  int idx3 = mensajeDecodificado.indexOf(SEPARADOR, idx2 + 1);
  int idx4 = mensajeDecodificado.indexOf(SEPARADOR, idx3 + 1);
  int idx5 = mensajeDecodificado.indexOf(SEPARADOR, idx4 + 1);
  
  if (idx1 <= 0 || idx2 <= 0 || idx3 <= 0 || idx4 <= 0 || idx5 <= 0) return false;
  
  tipo = mensajeDecodificado[0];
  origen = mensajeDecodificado.substring(idx1 + 1, idx2).toInt();
  destino = mensajeDecodificado.substring(idx2 + 1, idx3).toInt();
  ttl = mensajeDecodificado.substring(idx3 + 1, idx4).toInt();
  ruta = mensajeDecodificado.substring(idx4 + 1, idx5);
  contenido = mensajeDecodificado.substring(idx5 + 1);
  
  return true;
}

void enviarMensajeLoRa(String mensajeFormateado) {
  if (radio.transmit(mensajeFormateado) != RADIOLIB_ERR_NONE) {
    Serial.println("[ERROR] Envio fallido");
  }
}

// ============ FUNCIONES DE REENVÍO ============
void reenviarMensajeBroadcast(int origen, int ttl, const String& contenido, int remitenteOriginal) {
  if (ttl <= 0) return;
  
  int enviados = 0;
  for (auto& par : vecinos) {
    if (par.first == remitenteOriginal) continue;
    String ruta = String(par.first);
    enviarMensajeLoRa(formatearMensaje(TIPO_NORMAL, 0, origen, ttl - 1, ruta, contenido));
    enviados++;
  }
  if (enviados > 0) Serial.printf("[BROADCAST] Reenviado a %d vecinos\n", enviados);
}

void reenviarMensaje(int origen, int destino, int ttl, const String& ruta, const String& contenido, int siguienteSalto) {
  if (ttl <= 0) return;
  
  String nuevaRuta = ruta + String(SEPARADOR_RUTA) + String(siguienteSalto);
  enviarMensajeLoRa(formatearMensaje(TIPO_REDIRECCION, destino, origen, ttl - 1, nuevaRuta, contenido));
  Serial.printf("[MESH] Reenviado ID%d -> ID%d via ID%d\n", origen, destino, siguienteSalto);
  
  String info = "ID" + String(origen) + " > ID" + String(destino);
  mostrarMensajePantallaNoBlocking(">> REDIRIGIENDO <<", info);
  contadorRedirecciones++;
}

void enviarBeacon() {
  enviarMensajeLoRa(formatearMensaje(TIPO_BEACON, 0, DISPOSITIVO_ID, MAX_HOPS, "", ""));
}

void enviarRespuestaBeacon(int destino) {
  enviarMensajeLoRa(formatearMensaje(TIPO_RESPUESTA_BEACON, destino, DISPOSITIVO_ID, MAX_HOPS, "", ""));
}

// ============ FUNCIONES DE API ============
void registrarNodoEnServidor() {
  if (!wifiConnected) return;
  HTTPClient http;
  http.begin(String(django_server) + api_update_node);
  http.addHeader("Content-Type", "application/json");
  String jsonData = "{\"node_id\":" + String(DISPOSITIVO_ID) + ",\"zone_name\":\"" + ZONA_NODO + "\",\"battery_level\":" + String(getBatteryLevel()) + ",\"status\":\"online\"}";
  http.POST(jsonData);
  http.end();
}

void reportarEstadoNodo() {
  if (!wifiConnected) return;
  HTTPClient http;
  http.begin(String(django_server) + api_update_node + String(DISPOSITIVO_ID) + "/");
  http.addHeader("Content-Type", "application/json");
  String jsonData = "{\"status\":\"online\",\"battery_level\":" + String(getBatteryLevel()) + "}";
  http.PUT(jsonData);
  http.end();
}

void enviarMensajeAServidorConDestino(int origen, const String& remitente, const String& contenido, int destinoId, int destinoSubId) {
  if (!wifiConnected) {
    if (colaMensajesAPI.size() >= MAX_COLA_API) colaMensajesAPI.erase(colaMensajesAPI.begin());
    colaMensajesAPI.push_back({origen, remitente, contenido, destinoId, destinoSubId});
    return;
  }
  
  HTTPClient http;
  String url = String(django_server) + api_send_message;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<512> doc;
  doc["node_id"] = origen;
  doc["content"] = contenido;
  doc["zone_name"] = ZONA_NODO;
  doc["destino_id"] = destinoId;
  doc["destino_sub_id"] = destinoSubId;
  doc["is_emergency"] = false;
  doc["battery_level"] = getBatteryLevel();
  doc["sender"] = remitente;
  String jsonData;
  serializeJson(doc, jsonData);
  
  int code = http.POST(jsonData);
  if (code == 200 || code == 201) {
    Serial.printf("[API] Mensaje enviado: %s\n", contenido.c_str());
  } else {
    if (colaMensajesAPI.size() >= MAX_COLA_API) colaMensajesAPI.erase(colaMensajesAPI.begin());
    colaMensajesAPI.push_back({origen, remitente, contenido, destinoId, destinoSubId});
  }
  http.end();
}

void enviarMensajeAServidor(int origen, const String& remitente, const String& contenido) {
  enviarMensajeAServidorConDestino(origen, remitente, contenido, 0, 0);
}

void flushMensajesPendientesAPI() {
  if (!wifiConnected) return;
  if (colaMensajesAPI.empty()) return;

  std::vector<MensajePendienteAPI> pendientes = colaMensajesAPI;
  colaMensajesAPI.clear();
  for (auto &m : pendientes) {
    enviarMensajeAServidorConDestino(m.origen, m.remitente, m.contenido, m.destino_id, m.destino_sub_id);
  }
}

void sincronizarUsuarioEnServidor(const String& payloadJson) {
  if (!wifiConnected) return;

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, payloadJson);
  if (err) return;

  HTTPClient http;
  String url = String(django_server) + api_sync_user;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String jsonData;
  serializeJson(doc, jsonData);
  http.POST(jsonData);
  http.end();
}

void actualizarUbicacionUsuarioEnServidor(const String& username, int nodeId, int subId) {
  if (!wifiConnected) return;

  StaticJsonDocument<256> doc;
  doc["username"] = username;
  doc["node_id"] = nodeId;
  doc["sub_id"] = subId;

  HTTPClient http;
  String url = String(django_server) + api_update_user_location;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  String jsonData;
  serializeJson(doc, jsonData);
  http.POST(jsonData);
  http.end();
}

std::vector<String> obtenerUsuariosIndexLocal() {
  std::vector<String> result;
  if (!wifiConnected) return result;

  HTTPClient http;
  String url = String(django_server) + api_users_index;
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<8192> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      JsonArray arr = doc["users"].as<JsonArray>();
      for (JsonVariant v : arr) {
        result.push_back(v.as<String>());
      }
    }
  }
  http.end();
  return result;
}

String exportarUsuarioLocal(const String& username) {
  if (!wifiConnected) return "";

  HTTPClient http;
  String url = String(django_server) + api_user_export + "?username=" + username;
  http.begin(url);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return "";
  }
  String payload = http.getString();
  http.end();

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return "";
  String status = doc["status"].as<String>();
  if (status != "ok") return "";

  StaticJsonDocument<512> out;
  out["u"] = doc["u"].as<String>();
  out["p"] = doc["p"].as<String>();
  out["e"] = doc["e"].as<String>();
  String jsonOut;
  serializeJson(out, jsonOut);
  return jsonOut;
}

void enviarUserListChunksLoRa(int destinoNodo, const String& syncId) {
  std::vector<String> users = obtenerUsuariosIndexLocal();
  if (users.empty()) return;

  String csv = "";
  std::vector<String> chunks;
  const int maxChunkLen = 70;
  for (auto &u : users) {
    if (csv.length() == 0) {
      csv = u;
    } else if ((int)(csv.length() + 1 + u.length()) <= maxChunkLen) {
      csv += "," + u;
    } else {
      chunks.push_back(csv);
      csv = u;
    }
  }
  if (csv.length() > 0) chunks.push_back(csv);

  int total = (int)chunks.size();
  for (int i = 0; i < total; i++) {
    String msg = "USER_LIST_CHUNK|" + syncId + "|" + String(i + 1) + "/" + String(total) + "|" + chunks[i];
    enviarMensajeLoRa(formatearMensaje(TIPO_NORMAL, destinoNodo, DISPOSITIVO_ID, MAX_HOPS, "", msg));
  }
}

void enviarUserCreateChunksLoRa(int destinoNodo, const String& payloadJson) {
  const int chunkSize = 45;
  int total = (payloadJson.length() + chunkSize - 1) / chunkSize;
  if (total <= 0) return;
  String syncId = String((uint32_t)esp_random(), HEX);
  if (syncId.length() > 8) syncId = syncId.substring(0, 8);
  if (syncId.length() < 8) syncId = (syncId + "00000000").substring(0, 8);

  for (int i = 0; i < total; i++) {
    int start = i * chunkSize;
    String part = payloadJson.substring(start, start + chunkSize);
    String msg = "USER_CREATE_CHUNK|" + syncId + "|" + String(i + 1) + "/" + String(total) + "|" + part;
    enviarMensajeLoRa(formatearMensaje(TIPO_NORMAL, destinoNodo, DISPOSITIVO_ID, MAX_HOPS, "", msg));
  }
}
void obtenerMensajesDelServidor() {
  if (!wifiConnected) return;
  
  HTTPClient http;
  String url = String(django_server) + api_get_messages + "?node_id=" + String(DISPOSITIVO_ID);
  http.begin(url);
  int code = http.GET();
  
  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<4096> doc;
    deserializeJson(doc, payload);
    
    JsonArray messages = doc["messages"];
    for (JsonObject msg : messages) {
      String content = msg["content"].as<String>();
      int destinoId = msg["destino_id"] | 0;
      int destinoSubId = msg["destino_sub_id"] | 0;
      String sender = msg["sender"].as<String>();
      int msgId = msg["id"] | 0;
      
      // Manejar mensajes especiales de sistema
      if (content.startsWith("USER_MOVE:")) {
        // Formato: USER_MOVE:username:node_id.sub_id
        String data = content.substring(10);
        int colon = data.indexOf(':');
        if (colon > 0) {
          String user = data.substring(0, colon);
          String idStr = data.substring(colon + 1);
          int dot = idStr.indexOf('.');
          if (dot > 0) {
            int nid = idStr.substring(0, dot).toInt();
            int sid = idStr.substring(dot + 1).toInt();
            
            if (nid == DISPOSITIVO_ID) {
              registrarUsuarioLocal(user, sid);
              notificarCambioUsuario(user, sid);
            } else {
              tablaRuteoUsuarios[user] = {nid, sid, millis()};
              notificarUbicacionUsuario(user, nid, sid);
            }
            mostrarMensajePantallaNoBlocking("REENVIANDO USUARIO", "");
          }
        }
      } else {
        Serial.printf("[API] Mensaje para %d.%d de %s: %s\n", destinoId, destinoSubId, sender.c_str(), content.c_str());

        bool esLegacyUserCreate = content.startsWith("USER_CREATE|");
        bool esChunkUserCreate = content.startsWith("USER_CREATE_CHUNK|");

        if (esLegacyUserCreate) {
          mostrarMensajePantallaNoBlocking("REENVIANDO USUARIO", "");
          int sep = content.indexOf('|');
          if (sep > 0) {
            String payloadUser = content.substring(sep + 1);
            sincronizarUsuarioEnServidor(payloadUser);
          }
        } else {
          if (esChunkUserCreate) {
            mostrarMensajePantallaNoBlocking("REENVIANDO USUARIO", "");
          } else {
            agregarAlHistorial(sender, content, DISPOSITIVO_ID);
            mostrarMensajePantallaNoBlocking(sender, content);
          }

          String displayMsg;
          if (esChunkUserCreate) {
            displayMsg = content;
          } else if (destinoId != 0 && destinoSubId != 0) {
            displayMsg = "TO:" + String(destinoSubId) + "|" + sender + ": " + content;
          } else {
            displayMsg = sender + ": " + content;
          }
          String mensajeFormateado = formatearMensaje(TIPO_NORMAL, destinoId, DISPOSITIVO_ID, MAX_HOPS, "", displayMsg);
          ultimoEnvioTime = millis();
          enviarMensajeLoRa(mensajeFormateado);
        }
      }
      
      // Marcar como entregado en el servidor
      if (msgId > 0) {
          HTTPClient httpMark;
          String markUrl = String(django_server) + "/api/mark-delivered/" + String(msgId) + "/";
          httpMark.begin(markUrl);
          httpMark.addHeader("Content-Type", "application/json");
          String markData = "{\"node_id\":" + String(DISPOSITIVO_ID) + "}";
          httpMark.PUT(markData);
          httpMark.end();
        }
      }
    }
    http.end();
}

void conectarWiFi() {
  if (wifiConnected) return;
  
  Serial.println("\n[WiFi] Conectando a: " + String(wifi_ssid));
  display.clear();
  display.drawString(0, 0, "Conectando WiFi...");
  display.display();
  
  WiFi.begin(wifi_ssid, wifi_password);
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n[WiFi] Conectado! IP: " + WiFi.localIP().toString());
    registrarNodoEnServidor();
  } else {
    Serial.println("\n[WiFi] Error de conexion");
  }
}

// ============ PROCESAMIENTO DE MENSAJES RECIBIDOS ============
void procesarMensajeRecibido() {
  char tipo;
  int origen, destino, ttl;
  String ruta, contenido;
  
  if (!parsearMensaje(mensajeRecibido, tipo, origen, destino, ttl, ruta, contenido)) {
    Serial.println("[ERROR] CRC o Desencriptacion fallida");
    contadorCRCErroneos++;
    return;
  }
  
  if (origen != DISPOSITIVO_ID) actualizarVecino(origen, radio.getRSSI());
  
  switch(tipo) {
    case TIPO_BEACON:
      if (origen != DISPOSITIVO_ID) enviarRespuestaBeacon(origen);
      break;
      
    case TIPO_RESPUESTA_BEACON:
      // Vecino actualizado arriba
      break;
      
    case TIPO_ACK:
      Serial.printf("[ACK] Recibido de ID%d\n", origen);
      break;

    case TIPO_BUSQUEDA_USUARIO:
      // contenido = username buscado
      if (usuariosLocales.count(contenido)) {
        // Yo lo tengo! Responder con mis datos
        Serial.printf("[UNICAST] Peticion de busqueda para %s. YO lo tengo.\n", contenido.c_str());
        notificarCambioUsuario(contenido, usuariosLocales[contenido].sub_id);
      }
      break;

    case TIPO_NOTIFICACION_USUARIO:
      // contenido = username:node_id.sub_id
      {
        int colon = contenido.indexOf(':');
        int dot = contenido.indexOf('.');
        if (colon > 0 && dot > colon) {
          String user = contenido.substring(0, colon);
          int nid = contenido.substring(colon + 1, dot).toInt();
          int sid = contenido.substring(dot + 1).toInt();
          
          tablaRuteoUsuarios[user] = {nid, sid, millis()};
          Serial.printf("[UNICAST] Ruta actualizada: %s -> %d.%d\n", user.c_str(), nid, sid);

          actualizarUbicacionUsuarioEnServidor(user, nid, sid);
        }
      }
      break;

    case TIPO_NORMAL:
    case TIPO_REDIRECCION:
      if (destino == DISPOSITIVO_ID || destino == 0) {
        if (!esMensajeBroadcastDuplicado(origen, contenido)) {
          Serial.printf("[RECIBIDO] ID%d: %s\n", origen, contenido.c_str());
          
          // Extraer remitente y mensaje del formato "Remitente: mensaje"
          int destinoSubId = 0;
          String raw = contenido;
          if (raw.startsWith("TO:")) {
            int p = raw.indexOf('|');
            if (p > 3) {
              destinoSubId = raw.substring(3, p).toInt();
              raw = raw.substring(p + 1);
            }
          }

          String remitente = "ID" + String(origen);
          String mensajeTexto = raw;
          int dosPuntos = raw.indexOf(':');
          if (dosPuntos > 0) {
            remitente = raw.substring(0, dosPuntos);
            mensajeTexto = raw.substring(dosPuntos + 2);
          }

          if (mensajeTexto.startsWith("USER_LIST_REQ|")) {
            mostrarMensajePantallaNoBlocking("REENVIANDO USUARIO", "");
            int p1 = mensajeTexto.indexOf('|');
            if (p1 > 0) {
              String syncId = mensajeTexto.substring(p1 + 1);
              enviarUserListChunksLoRa(origen, syncId);
            }
          } else if (mensajeTexto.startsWith("USER_GET|")) {
            mostrarMensajePantallaNoBlocking("REENVIANDO USUARIO", "");
            int p1 = mensajeTexto.indexOf('|');
            if (p1 > 0) {
              String username = mensajeTexto.substring(p1 + 1);
              String payload = exportarUsuarioLocal(username);
              if (payload.length() > 0) {
                enviarUserCreateChunksLoRa(origen, payload);
              }
            }
          } else if (mensajeTexto.startsWith("USER_LIST_CHUNK|")) {
            mostrarMensajePantallaNoBlocking("REENVIANDO USUARIO", "");
            int p1 = mensajeTexto.indexOf('|');
            int p2 = mensajeTexto.indexOf('|', p1 + 1);
            int p3 = mensajeTexto.indexOf('|', p2 + 1);
            if (p1 > 0 && p2 > p1 && p3 > p2) {
              String syncId = mensajeTexto.substring(p1 + 1, p2);
              String idxTot = mensajeTexto.substring(p2 + 1, p3);
              String chunk = mensajeTexto.substring(p3 + 1);
              int slash = idxTot.indexOf('/');
              if (slash > 0) {
                int idx = idxTot.substring(0, slash).toInt();
                int tot = idxTot.substring(slash + 1).toInt();
                if (tot > 0 && idx > 0 && idx <= tot) {
                  if (!userListChunksTotal.count(syncId)) {
                    userListChunksTotal[syncId] = tot;
                    userListChunks[syncId] = std::vector<String>(tot);
                    userListResponder[syncId] = origen;
                  }
                  if (userListChunksTotal[syncId] == tot) {
                    userListChunks[syncId][idx - 1] = chunk;
                    userListChunksTs[syncId] = millis();
                    bool completo = true;
                    for (int i = 0; i < tot; i++) {
                      if (userListChunks[syncId][i].length() == 0) { completo = false; break; }
                    }
                    if (completo) {
                      String csv = "";
                      for (int i = 0; i < tot; i++) csv += userListChunks[syncId][i];
                      std::vector<String> local = obtenerUsuariosIndexLocal();
                      std::map<String, bool> localSet;
                      for (auto &u : local) localSet[u] = true;

                      int responder = userListResponder[syncId];
                      int start = 0;
                      while (start < (int)csv.length()) {
                        int comma = csv.indexOf(',', start);
                        String u = (comma == -1) ? csv.substring(start) : csv.substring(start, comma);
                        u.trim();
                        if (u.length() > 0 && !localSet.count(u)) {
                          String req = "USER_GET|" + u;
                          enviarMensajeLoRa(formatearMensaje(TIPO_NORMAL, responder, DISPOSITIVO_ID, MAX_HOPS, "", req));
                        }
                        if (comma == -1) break;
                        start = comma + 1;
                      }

                      userListChunks.erase(syncId);
                      userListChunksTotal.erase(syncId);
                      userListChunksTs.erase(syncId);
                      userListResponder.erase(syncId);
                    }
                  }
                }
              }
            }
          } else if (mensajeTexto.startsWith("USER_CREATE|")) {
            mostrarMensajePantallaNoBlocking("REENVIANDO USUARIO", "");
            int sep = mensajeTexto.indexOf('|');
            if (sep > 0) {
              String payload = mensajeTexto.substring(sep + 1);
              sincronizarUsuarioEnServidor(payload);
            }
          } else if (mensajeTexto.startsWith("USER_CREATE_CHUNK|")) {
            mostrarMensajePantallaNoBlocking("REENVIANDO USUARIO", "");
            int p1 = mensajeTexto.indexOf('|');
            int p2 = mensajeTexto.indexOf('|', p1 + 1);
            int p3 = mensajeTexto.indexOf('|', p2 + 1);
            if (p1 > 0 && p2 > p1 && p3 > p2) {
              String syncId = mensajeTexto.substring(p1 + 1, p2);
              String idxTot = mensajeTexto.substring(p2 + 1, p3);
              String chunk = mensajeTexto.substring(p3 + 1);
              int slash = idxTot.indexOf('/');
              if (slash > 0) {
                int idx = idxTot.substring(0, slash).toInt();
                int tot = idxTot.substring(slash + 1).toInt();
                if (tot > 0 && idx > 0 && idx <= tot) {
                  if (!userCreateChunksTotal.count(syncId)) {
                    userCreateChunksTotal[syncId] = tot;
                    userCreateChunks[syncId] = std::vector<String>(tot);
                  }
                  if (userCreateChunksTotal[syncId] == tot) {
                    userCreateChunks[syncId][idx - 1] = chunk;
                    userCreateChunksTs[syncId] = millis();
                    bool completo = true;
                    for (int i = 0; i < tot; i++) {
                      if (userCreateChunks[syncId][i].length() == 0) { completo = false; break; }
                    }
                    if (completo) {
                      String payload = "";
                      for (int i = 0; i < tot; i++) payload += userCreateChunks[syncId][i];
                      sincronizarUsuarioEnServidor(payload);
                      userCreateChunks.erase(syncId);
                      userCreateChunksTotal.erase(syncId);
                      userCreateChunksTs.erase(syncId);
                    }
                  }
                }
              }
            }
          } else {
            agregarAlHistorial(remitente, mensajeTexto, origen);
            mostrarMensajePantallaNoBlocking(remitente, mensajeTexto);
            if (destino != 0 && destinoSubId != 0) {
              enviarMensajeAServidorConDestino(origen, remitente, mensajeTexto, destino, destinoSubId);
            } else {
              enviarMensajeAServidor(origen, remitente, mensajeTexto);
            }
            contadorMensajes++;
          }
        }
      } else if (origen != DISPOSITIVO_ID && ttl > 0 && !esMensajeUnicastDuplicado(origen, destino, contenido)) {
        mensajesPendientes[String(origen) + ":" + String(destino)] = {contenido, origen, destino, ttl, ruta, millis()};
        int siguienteSalto = encontrarMejorVecino(destino, origen);
        if (siguienteSalto != -1) reenviarMensaje(origen, destino, ttl, ruta, contenido, siguienteSalto);
      }
      break;
  }
}

// ============ SETUP ============
void setup() {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(200);
  heltec_setup();
  Serial.begin(115200);
  
  display.clear();
  display.drawString(0, 0, "SisME LoRa Mesh v2.0");
  display.drawString(0, 16, "Iniciando...");
  display.drawString(0, 32, "ID: " + String(DISPOSITIVO_ID));
  display.display();
  delay(1000);
  
  conectarWiFi();
  
  if (radio.begin(FREQUENCY) != RADIOLIB_ERR_NONE) {
    display.clear();
    display.drawString(0, 0, "ERROR RADIO!");
    display.display();
    while (true);
  }
  
  Serial.println("=====================================");
  Serial.println("SisME LoRa Mesh Node v2.0");
  Serial.print("ID: "); Serial.println(DISPOSITIVO_ID);
  Serial.println("=====================================");
  
  radio.setPacketReceivedAction([]() { mensajeNuevo = true; });
  radio.startReceive();
  actualizarPantallaChat();
}

// ============ LOOP ============
void loop() {
  heltec_loop();
  
  if (!wifiConnected && millis() - ultimoIntentoWiFi >= INTERVALO_INTENTO_WIFI) {
    ultimoIntentoWiFi = millis();
    conectarWiFi();
  }
  
  if (millis() - ultimaLimpiezaVecinos >= INTERVALO_LIMPIEZA_VECINOS) {
    ultimaLimpiezaVecinos = millis();
    limpiarVecinosInactivos();
  }
  
  if (millis() - ultimoBeaconEnviado >= INTERVALO_BEACON) {
    ultimoBeaconEnviado = millis();
    enviarBeacon();
  }
  
  if (wifiConnected && millis() - ultimoReporteNodo >= INTERVALO_REPORTE_NODO) {
    ultimoReporteNodo = millis();
    reportarEstadoNodo();
  }
  
  if (wifiConnected && millis() - ultimaSincronizacionAPI >= INTERVALO_SINCRONIZACION_API) {
    ultimaSincronizacionAPI = millis();
    flushMensajesPendientesAPI();
    obtenerMensajesDelServidor();
  }

  for (auto it = userCreateChunksTs.begin(); it != userCreateChunksTs.end();) {
    if (millis() - it->second > TIMEOUT_USER_CREATE_CHUNKS) {
      String syncId = it->first;
      userCreateChunks.erase(syncId);
      userCreateChunksTotal.erase(syncId);
      it = userCreateChunksTs.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = userListChunksTs.begin(); it != userListChunksTs.end();) {
    if (millis() - it->second > TIMEOUT_USER_CREATE_CHUNKS) {
      String syncId = it->first;
      userListChunks.erase(syncId);
      userListChunksTotal.erase(syncId);
      userListResponder.erase(syncId);
      it = userListChunksTs.erase(it);
    } else {
      ++it;
    }
  }
  
  if (mostrandoMensajeTemporal) {
    if (millis() - tiempoInicioMensajePantalla >= DURACION_MENSAJE_PANTALLA) {
      mostrandoMensajeTemporal = false;
      actualizarPantallaChat();
    }
  } else {
    actualizarPantallaChat();
  }
  
  if (mensajeNuevo) {
    mensajeNuevo = false;
    if (radio.readData(mensajeRecibido) == RADIOLIB_ERR_NONE && mensajeRecibido.length() > 0) {
      procesarMensajeRecibido();
    }
    radio.startReceive();
  }
  
  if (Serial.available() > 0) {
    String entrada = Serial.readStringUntil('\n');
    entrada.trim();
    if (entrada.length() > 0) {
      if (entrada.equalsIgnoreCase("SYNCUSERS")) {
        String syncId = String((uint32_t)esp_random(), HEX);
        if (syncId.length() > 8) syncId = syncId.substring(0, 8);
        if (syncId.length() < 8) syncId = (syncId + "00000000").substring(0, 8);
        String req = "USER_LIST_REQ|" + syncId;
        enviarMensajeLoRa(formatearMensaje(TIPO_NORMAL, 0, DISPOSITIVO_ID, MAX_HOPS, "", req));
        mostrarMensajePantallaNoBlocking("REENVIANDO USUARIO", "");
        return;
      }
      String displayMsg = "Yo: " + entrada;
      String msg = formatearMensaje(TIPO_NORMAL, 0, DISPOSITIVO_ID, MAX_HOPS, "", displayMsg);
      enviarMensajeLoRa(msg);
      agregarAlHistorial("Yo", entrada, DISPOSITIVO_ID);
      enviarMensajeAServidor(DISPOSITIVO_ID, "Yo", entrada);
    }
  }
  
  for (auto it = mensajesPendientes.begin(); it != mensajesPendientes.end();) {
    if (millis() - it->second.timestamp > TIMEOUT_MENSAJES_PENDIENTES) {
      it = mensajesPendientes.erase(it);
    } else {
      ++it;
    }
  }
  
  heltec_delay(10);
}
