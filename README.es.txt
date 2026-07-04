📡 SisME - Sistema de Mensajería de Emergencia
Comunicación crítica basada en LoRa y el protocolo CALM.

🔑 Palabras Clave
LoRa ESP32 Red en Malla Comunicación de Emergencia Django Protocolo CALM IoT Recuperación ante Desastres AES-128 Comunicación por Radio Bootstrap 5 SX1276 Heltec Arduino Capa de Enlace de Datos RSSI Broadcast Unicast Protocolo de Capa 2

[License: GPL v3]

📖 Descripción General
SisME (Sistema de Mensajería de Emergencia) es una plataforma de comunicación descentralizada diseñada para situaciones críticas donde las redes tradicionales (WiFi, Internet) fallan. Combina una interfaz web amigable con una robusta malla de radiofrecuencia LoRa que opera bajo el protocolo CALM.

CALM es un protocolo RAW de capa 2 (nivel de enlace de datos) desarrollado específicamente para este sistema, que permite:

    - Enrutamiento dinámico en malla a nivel de capa 2.
    - Mensajería broadcast y unicast.
    - Encriptación AES-128 de extremo a extremo.
    - Detección de duplicados y control de TTL (Time To Live).
    - Sincronización automática de usuarios entre nodos.

Creado por: SasakiRyo27 (Massimo Larger) & DonCaut (Claudio Uribe)

🚀 Características Principales

SisME (Aplicación Web)
    - Chat de Emergencia Web: Interfaz en tiempo real para usuarios registrados.
    - Mensajería Unicast y Broadcast: Comunicación grupal o privada.
    - Panel de Monitoreo: Estado de nodos LoRa, batería y actividad de red.
    - Registro Inteligente: Asignación automática a nodos LoRa disponibles.
    - Perfiles de Usuario: Edición de datos personales y visualización de ID LoRa.

CALM (Protocolo RAW de Capa 2)
    - Red en Malla Auto-Configurable: Los nodos descubren vecinos dinámicamente mediante beacons.
    - Enrutamiento Adaptativo: Reenvío de mensajes a través de la mejor ruta disponible basada en RSSI.
    - Encriptación AES-128: Todos los mensajes viajan cifrados a nivel de capa 2.
    - Control de Duplicados: Previene inundación de la red con mensajes repetidos.
    - Sincronización de Usuarios: Los nodos comparten información de usuarios registrados.
    - Mensajes de Sistema: Soporte para comandos especiales (USER_MOVE, USER_CREATE, etc.).

🏗️ Arquitectura del Sistema
El sistema se compone de tres capas principales:

1. Backend Web (SisME Django):
    - Base de datos SQLite/PostgreSQL.
    - API REST para recibir/enviar mensajes y gestionar nodos.
    - Panel de administración para monitoreo.

2. Frontend Web (SisME UI):
    - HTML, CSS, Bootstrap 5.
    - JavaScript para chat en tiempo real y monitoreo de nodos.

3. Hardware (Nodos LoRa con CALM):
    - Dispositivos basados en Heltec ESP32.
    - Firmware en C++ (Arduino) implementando el protocolo CALM en capa 2.
    - Comunicación por LoRa en la banda de 915MHz.

🛠️ Tecnologías Utilizadas

SisME (Backend)
    - Django 5.2: Framework web.
    - SQLite: Base de datos por defecto.
    - Django REST Framework: API para nodos LoRa.

SisME (Frontend)
    - Bootstrap 5: Estilos y componentes responsive.
    - Bootstrap Icons: Iconografía.
    - JavaScript Fetch API: Comunicación asíncrona con el servidor.

CALM (Firmware - Arduino/ESP32)
    - Heltec ESP32 + SX1276: Hardware de los nodos.
    - Librería RadioLib: Gestión de la comunicación LoRa.
    - mbedtls: Encriptación AES-128.
    - Protocolo CALM: Capa 2 RAW personalizada.

📡 Protocolo CALM (Capa 2)
El protocolo CALM opera a nivel de capa 2 (enlace de datos) y define la estructura de los frames transmitidos por LoRa:

Formato de Frame CALM

[TIPO]:[ORIGEN]:[DESTINO]:[TTL]:[RUTA]:[CONTENIDO]|[CRC]

- TIPO: 	N (Normal), R (Redirección), B (Beacon), E (Respuesta Beacon), Q (Búsqueda Usuario), U (Notificación Usuario).
- ORIGEN: 	ID del nodo que envió el frame.
- DESTINO: 	ID del nodo destinatario (0 = Broadcast).
- TTL: 		Tiempo de vida (hops restantes).
- RUTA: 	Lista de nodos por los que ha pasado el frame.
- CONTENIDO: 	Payload del mensaje o comando de sistema.
- CRC: 		Código de verificación de integridad (capa 2).

Tipos de Frames CALM
Tipo	Descripción
N	Frame Normal (usuario o sistema)
R	Redirección (frame en tránsito)
B	Beacon (detección de vecinos)
E	Respuesta Beacon
Q	Búsqueda de Usuario en la malla
U 	Notificación de Ubicación de Usuario

Características de Capa 2
    - Detección de Vecinos: Beacons periódicos para mantener la tabla de vecinos.
    - Control de Errores: CRC-16 para integridad de frames.
    - Control de Flujo: TTL para evitar loops infinitos.
    - Direccionamiento: IDs de nodos de 8 bits (0-255).

⚙️ Instalación y Configuración

1. Clonar el Repositorio

    git clone https://github.com/SasakiRyo27/SisME-LoRa-Protocol-CALM
    cd SisME-LoRa-Protocol-CALM

2. Backend (Servidor Django - SisME)

Requisitos: Python 3.10+

Crear y activar un entorno virtual:
    python -m venv venv
    source venv/bin/activate  # En Windows: venv\Scripts\activate

Instalar las dependencias:
    pip install django

Configurar la base de datos:
    python manage.py migrate

Crear un superusuario para el admin:
    python manage.py createsuperuser

Ejecutar el servidor de desarrollo:
    python manage.py runserver 0.0.0.0:8000

3. Frontend (Interfaz Web SisME)
    - Los archivos estáticos (CSS, JS, imágenes) se sirven automáticamente en modo DEBUG.
    - Para producción, ejecuta python manage.py collectstatic.

4. Firmware (Nodos Heltec con CALM)
Requisitos: Arduino IDE o PlatformIO.

Abrir cualquiera de los siguientes archivos .ino:
    - heltec_master_node.ino: Nodo Gateway/Master (conectado a WiFi, puente entre SisME y la red CALM).
    - heltec_relay_node.ino: Nodo Relay/Repetidor (solo reenvía frames CALM).
    - heltec_emisor.ino: Nodo Emisor (envía mensajes desde el propio dispositivo).

Configurar el DISPOSITIVO_ID de forma única para cada nodo (1, 2, 3...).

En el heltec_master_node.ino, configurar las credenciales WiFi y la IP del servidor Django:

    const char* wifi_ssid = "TuRedWiFi";
    const char* wifi_password = "TuContraseña";
    const char* django_server = "http://x.x.x.x:8000"; // IP del servidor SisME
    Cargar el código al dispositivo Heltec.

📱 Uso del Sistema
SisME (Web)
    Accede a http://localhost:8000 o a la IP de tu servidor.
    Regístrate o inicia sesión.
    El sistema te asignará automáticamente a un nodo LoRa con un ID tipo X.Y (nodo.sub-id).
    Usa el chat general para Broadcast o busca usuarios para Mensajes Privados.
    Monitorea el estado de la red CALM en el panel lateral derecho.

Nodos LoRa (CALM)
    Monitor Serial: Conecta por USB y abre el monitor serial (115200 baudios).
    Enviar frame CALM: Escribe el mensaje en la consola serial y presiona Enter. Luego, ingresa el ID de destino (0 para Broadcast).
    Pantalla OLED: Muestra los mensajes entrantes, el estado de la batería y los vecinos detectados.

Comandos Especiales CALM (vía Serial)
    SYNCUSERS: Fuerza la sincronización de la lista de usuarios en toda la malla.

📁 Estructura del Proyecto

SisME-LoRa-Protocol-CALM/
├── chat/                        # Aplicación Django (SisME)
│   ├── migrations/               # Migraciones de BD
│   ├── templates/                # Plantillas HTML
│   │   ├── chat/
│   │   │   └── chat_room.html
│   │   └── registration/
│   │       ├── login.html
│   │       ├── register.html
│   │       ├── register_success.html
│   │       └── edit_profile.html
│   ├── static/                   # Archivos estáticos
│   │   ├── css/
│   │   ├── icons/
│   │   └── js/
│   ├── admin.py                 # Configuración del admin
│   ├── models.py                # Modelos de datos
│   ├── views.py                 # Lógica de las vistas
│   ├── urls.py                  # Rutas de la aplicación
│   └── ...
├── sme_lora_project/            # Configuración del proyecto Django
│   ├── settings.py
│   ├── urls.py
│   └── ...
├── hardware/                    # Código fuente para nodos CALM
│   ├── heltec_master_node.ino   # Nodo Gateway (conecta SisME con CALM)
│   ├── heltec_relay_node.ino    # Nodo Relay (repite frames CALM)
│   └── heltec_emisor.ino        # Nodo Emisor (envía frames CALM)
└── manage.py

📡 API REST Endpoints (SisME)
El backend expone una API para que los nodos LoRa se comuniquen:

Método	Endpoint	                Descripción
POST	/api/send-message/	        Envía un mensaje desde un nodo LoRa al servidor.
GET	/api/get-messages/	        Obtiene mensajes pendientes para un nodo.
POST	/api/update-node/	        Registra o actualiza un nodo LoRa.
PUT	/api/update-node/<id>/	    	Actualiza el estado de un nodo.
PUT	/api/mark-delivered/<id>/	Marca un mensaje como entregado.
GET	/api/node-status/	        Obtiene el estado de todos los nodos.
GET	/api/users-index/	        Lista todos los usuarios registrados.
POST	/api/sync-user/	            	Sincroniza un usuario en el servidor.
POST	/api/update-user-location/	Actualiza la ubicación de un usuario.

🤝 Contribuciones
Las contribuciones son bienvenidas. Abre primero un issue para discutir que te gustaría implementar.

📜 Licencia
Este proyecto está bajo la Licencia GPL-3.0, consulte el archivo LICENSE para más detalles.

👨‍💻 Créditos
Massimo Larger y Claudio Uribe
    - Desarrollo del firmware e implementación del protocolo CALM (capa 2).
    - Desarrollo del backend Django (SisME) y la interfaz web.

📧 Contacto
Para cualquier consulta o sugerencia, no dudes en contactarnos a los correos.
	massimoalv.27@gmail.com/

¡SisME + CALM: Comunicación de emergencia para cuando más se necesita! 📡🆘

SisME - Sistema de Mensajería de Emergencia
Copyright (C) 2026  Massimo Larger & Claudio Uribe

Este programa es software libre: puedes redistribuirlo y/o modificarlo
bajo los términos de la Licencia Pública General de GNU publicada por
la Free Software Foundation, ya sea la versión 3 de la Licencia, o
(a su elección) cualquier versión posterior.

Este programa se distribuye con la esperanza de que sea útil.
Consulte la Licencia Pública General de GNU para obtener más detalles.