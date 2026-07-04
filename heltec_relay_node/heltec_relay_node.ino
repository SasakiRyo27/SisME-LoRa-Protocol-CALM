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

//Esta version es solo para nodos repetidores, que no necesiten conectarse al servidor. Los nodos maestros deben usar la version heltec_master_node.ino

#define FREQUENCY 915.0

const uint8_t AES_KEY[16] = {
  0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
  0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

#define DISPOSITIVO_ID 2
#define ZONA_NODO "Repetidor SUR"

#define SEPARADOR ':'
#define SEPARADOR_CRC '|'
#define SEPARADOR_ENCRIPTADO '#'
#define SEPARADOR_RUTA '>'

#define TIPO_NORMAL 'N'
#define TIPO_REDIRECCION 'R'
#define TIPO_ACK 'A'
#define TIPO_BEACON 'B'
#define TIPO_RESPUESTA_BEACON 'E'

#define MAX_HOPS 10
#define VECINOS_MAX 5
#define TIMEOUT_VECINO 30000
#define TIEMPO_RETENCION_HASH 60000
#define TIMEOUT_MENSAJES_PENDIENTES 10000
#define MIN_INTERVALO_UPDATE_VECINO 100

#define INTERVALO_BEACON 10000
#define INTERVALO_LIMPIEZA_VECINOS 15000

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

std::map<int, Vecino> vecinos;
std::map<String, MensajeEnRuta> mensajesPendientes;
std::vector<MensajeBroadcastVisto> mensajesBroadcastVistos;

String mensajeRecibido = "";
bool mensajeNuevo = false;

unsigned long ultimoBeaconEnviado = 0;
unsigned long ultimaLimpiezaVecinos = 0;

int contadorRedirecciones = 0;
int contadorCRCErroneos = 0;

bool mostrandoMensajeTemporal = false;
unsigned long tiempoInicioMensajePantalla = 0;
const unsigned long DURACION_MENSAJE_PANTALLA = 2500;

struct MensajeChat {
  String texto;
  String remitente;
  int idOrigen;
  unsigned long timestamp;
};

MensajeChat historialChat[10];
int totalMensajesHistorial = 0;

int getBatteryLevel() {
  int battery = heltec_battery_percent();
  if (battery <= 0 || battery < 5) return 100;
  return battery > 100 ? 100 : battery;
}

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
  if (input_len > 240) return "";
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
  if (encrypted_len > 256) return "";
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

void limpiarVecinosInactivos() {
  unsigned long ahora = millis();
  for (auto it = vecinos.begin(); it != vecinos.end();) {
    if (ahora - it->second.ultimoBeacon >= TIMEOUT_VECINO) {
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
    return;
  }
  if (vecinos.size() < VECINOS_MAX) {
    vecinos[id] = {id, rssi, ahora, true};
    return;
  }
  int peorRssi = rssi;
  int peorId = -1;
  for (auto& par : vecinos) {
    if (par.second.rssi < peorRssi) {
      peorRssi = par.second.rssi;
      peorId = par.first;
    }
  }
  if (peorId != -1) {
    vecinos.erase(peorId);
    vecinos[id] = {id, rssi, ahora, true};
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

String generarHashMensaje(int origen, const String& contenido) {
  uint16_t hash = calcularCRC(String(origen) + ":" + contenido);
  char hashStr[9];
  snprintf(hashStr, sizeof(hashStr), "%04X_%lu", hash, millis() / 1000);
  return String(hashStr);
}

bool esMensajeBroadcastDuplicado(int origen, const String& contenido) {
  unsigned long ahora = millis();
  for (auto it = mensajesBroadcastVistos.begin(); it != mensajesBroadcastVistos.end();) {
    if (ahora - it->timestamp > TIEMPO_RETENCION_HASH) it = mensajesBroadcastVistos.erase(it);
    else ++it;
  }
  for (const auto& visto : mensajesBroadcastVistos) {
    if (visto.contenido == contenido && (ahora - visto.timestamp) < 5000) return true;
  }
  mensajesBroadcastVistos.push_back({generarHashMensaje(origen, contenido), contenido, ahora});
  return false;
}

bool esMensajeUnicastDuplicado(int origen, int destino, const String& contenido) {
  String key = String(origen) + ":" + String(destino);
  auto it = mensajesPendientes.find(key);
  if (it != mensajesPendientes.end()) {
    if (millis() - it->second.timestamp < 5000 && it->second.contenido == contenido) return true;
  }
  return false;
}

void agregarAlHistorial(String remitente, String mensaje, int idOrigen) {
  if (totalMensajesHistorial >= 10) {
    for (int i = 0; i < 9; i++) historialChat[i] = historialChat[i + 1];
    totalMensajesHistorial = 9;
  }
  historialChat[totalMensajesHistorial].texto = mensaje;
  historialChat[totalMensajesHistorial].remitente = remitente;
  historialChat[totalMensajesHistorial].idOrigen = idOrigen;
  historialChat[totalMensajesHistorial].timestamp = millis();
  totalMensajesHistorial++;
}

void actualizarPantallaChat() {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "SisME Relay");
  display.drawString(88, 0, String(getBatteryLevel()) + "%");
  display.drawString(0, 11, "ID:" + String(DISPOSITIVO_ID) + " [LoRa]");
  display.drawString(88, 11, "V:" + String(vecinos.size()));
  display.drawLine(0, 23, 128, 23);
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

String formatearMensaje(char tipo, int destino, int origen, int ttl, const String& ruta, const String& contenido) {
  String mensajeBase = String(tipo) + SEPARADOR + String(origen) + SEPARADOR + String(destino) + SEPARADOR + String(ttl) + SEPARADOR + ruta + SEPARADOR + contenido;
  String mensajeFinal = (tipo == TIPO_BEACON || tipo == TIPO_RESPUESTA_BEACON) ? mensajeBase : String(SEPARADOR_ENCRIPTADO) + encriptarAES(mensajeBase);
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
  radio.transmit(mensajeFormateado);
}

void reenviarMensajeBroadcast(int origen, int ttl, const String& contenido) {
  if (ttl <= 0) return;
  for (auto& par : vecinos) {
    String ruta = String(par.first);
    enviarMensajeLoRa(formatearMensaje(TIPO_NORMAL, 0, origen, ttl - 1, ruta, contenido));
  }
}

void reenviarMensaje(int origen, int destino, int ttl, const String& ruta, const String& contenido, int siguienteSalto) {
  if (ttl <= 0) return;
  String nuevaRuta = ruta + String(SEPARADOR_RUTA) + String(siguienteSalto);
  enviarMensajeLoRa(formatearMensaje(TIPO_REDIRECCION, destino, origen, ttl - 1, nuevaRuta, contenido));
  contadorRedirecciones++;
  String info = "ID" + String(origen) + " > ID" + String(destino);
  mostrarMensajePantallaNoBlocking(">> REDIRIGIENDO <<", info);
}

void enviarBeacon() {
  enviarMensajeLoRa(formatearMensaje(TIPO_BEACON, 0, DISPOSITIVO_ID, MAX_HOPS, "", ""));
}

void enviarRespuestaBeacon(int destino) {
  enviarMensajeLoRa(formatearMensaje(TIPO_RESPUESTA_BEACON, destino, DISPOSITIVO_ID, MAX_HOPS, "", ""));
}

void procesarMensajeRecibido() {
  char tipo;
  int origen, destino, ttl;
  String ruta, contenido;
  if (!parsearMensaje(mensajeRecibido, tipo, origen, destino, ttl, ruta, contenido)) {
    contadorCRCErroneos++;
    return;
  }

  if (origen != DISPOSITIVO_ID) actualizarVecino(origen, radio.getRSSI());

  switch (tipo) {
    case TIPO_BEACON:
      if (origen != DISPOSITIVO_ID) enviarRespuestaBeacon(origen);
      break;

    case TIPO_RESPUESTA_BEACON:
      break;

    case TIPO_ACK:
      break;

    case TIPO_NORMAL:
    case TIPO_REDIRECCION:
      if (destino == DISPOSITIVO_ID) {
        if (!esMensajeBroadcastDuplicado(origen, contenido)) {
          agregarAlHistorial("ID" + String(origen), contenido, origen);
          mostrarMensajePantallaNoBlocking("ID" + String(origen), contenido);
        }
        break;
      }

      if (destino == 0) {
        if (!esMensajeBroadcastDuplicado(origen, contenido)) {
          agregarAlHistorial("ID" + String(origen), contenido, origen);
          mostrarMensajePantallaNoBlocking("ID" + String(origen), contenido);
          if (ttl > 0) reenviarMensajeBroadcast(origen, ttl, contenido);
        }
        break;
      }

      if (origen != DISPOSITIVO_ID && ttl > 0 && !esMensajeUnicastDuplicado(origen, destino, contenido)) {
        mensajesPendientes[String(origen) + ":" + String(destino)] = {contenido, origen, destino, ttl, ruta, millis()};
        int siguienteSalto = encontrarMejorVecino(destino, origen);
        if (siguienteSalto != -1) reenviarMensaje(origen, destino, ttl, ruta, contenido, siguienteSalto);
      }
      break;
  }
}

void setup() {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(200);
  heltec_setup();
  Serial.begin(115200);

  display.clear();
  display.drawString(0, 0, "SisME Relay");
  display.drawString(0, 16, "Iniciando...");
  display.drawString(0, 32, "ID: " + String(DISPOSITIVO_ID));
  display.display();
  delay(800);

  if (radio.begin(FREQUENCY) != RADIOLIB_ERR_NONE) {
    display.clear();
    display.drawString(0, 0, "ERROR RADIO!");
    display.display();
    while (true);
  }

  radio.setPacketReceivedAction([]() { mensajeNuevo = true; });
  radio.startReceive();
  actualizarPantallaChat();
}

void loop() {
  heltec_loop();

  if (millis() - ultimaLimpiezaVecinos >= INTERVALO_LIMPIEZA_VECINOS) {
    ultimaLimpiezaVecinos = millis();
    limpiarVecinosInactivos();
  }

  if (millis() - ultimoBeaconEnviado >= INTERVALO_BEACON) {
    ultimoBeaconEnviado = millis();
    enviarBeacon();
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

  for (auto it = mensajesPendientes.begin(); it != mensajesPendientes.end();) {
    if (millis() - it->second.timestamp > TIMEOUT_MENSAJES_PENDIENTES) it = mensajesPendientes.erase(it);
    else ++it;
  }

  heltec_delay(10);
}
