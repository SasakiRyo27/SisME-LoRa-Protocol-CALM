📡 SisME - Emergency Messaging System
Critical Communication based on LoRa and the CALM Protocol

Keywords
LoRa ESP32 Mesh Network Emergency Communication Django CALM Protocol IoT Disaster Recovery AES-128 Radio Communication Bootstrap 5 SX1276 Heltec Arduino Data Link Layer RSSI Broadcast Unicast Layer 2 Protocol

[License: GPL v3]

📖 Project Description
SisME (Emergency Messaging System) is a decentralized communication platform designed for critical situations where traditional networks (WiFi, Internet) fail. It combines a user-friendly web interface with a robust LoRa radio frequency mesh operating under the CALM protocol.

CALM is a custom Layer 2 (Data Link Layer) RAW protocol developed specifically for this system, enabling:
  - Dynamic Layer 2 mesh routing
  - Broadcast and unicast messaging
  - End-to-end AES-128 encryption
  - Duplicate detection and TTL (Time To Live) control
  - Automatic user synchronization between nodes

Created by: SasakiRyo27 (Massimo Larger) & DonCaut (Claudio Uribe)

🚀 Key Features
SisME (Web Application)
  - Emergency Web Chat: Real-time interface for registered users
  - Unicast & Broadcast Messaging: Group or private communication
  - Monitoring Dashboard: LoRa node status, battery levels, and network activity
  - Smart Registration: Automatic assignment to available LoRa nodes
  - User Profiles: Personal data editing and LoRa ID visualization

CALM (Layer 2 RAW Protocol)
  - Self-Configuring Mesh Network: Dynamic neighbor discovery via beacons
  - Adaptive Routing: Message forwarding through the best available route based on RSSI
  - AES-128 Encryption: All messages encrypted at Layer 2
  - Duplicate Control: Prevents network flooding with repeated messages
  - User Synchronization: Nodes share registered user information
  - System Messages: Support for special commands (USER_MOVE, USER_CREATE, etc.)

🏗️ System Architecture
The system consists of three main layers:

1. Backend Web (SisME Django):
  - SQLite/PostgreSQL database
  - REST API for message handling and node management
  - Admin panel for monitoring

2. Frontend Web (SisME UI):
  - HTML, CSS, Bootstrap 5
  - JavaScript for real-time chat and node monitoring

3. Hardware (LoRa Nodes with CALM):
  - Heltec ESP32-based devices
  - C++ firmware (Arduino) implementing the CALM protocol at Layer 2
  - LoRa communication in the 915MHz band

🛠️ Technologies Used
Backend
  - Django 5.2: Web framework
  - SQLite: Default database
  - Django REST Framework: API for LoRa nodes
  - Python 3.10+: Programming language

Frontend
  - Bootstrap 5: Responsive styles and components
  - Bootstrap Icons: Iconography
  - JavaScript Fetch API: Asynchronous server communication

Firmware (Arduino/ESP32)
  - Heltec ESP32 + SX1276: Node hardware
  - RadioLib Library: LoRa communication management
  - mbedtls: AES-128 encryption
  - CALM Protocol: Custom Layer 2 RAW protocol

📡 CALM Protocol (Layer 2)
The CALM protocol operates at Layer 2 (Data Link Layer) and defines the structure of LoRa-transmitted frames:

Frame Format
[TYPE]:[ORIGIN]:[DESTINATION]:[TTL]:[ROUTE]:[CONTENT]|[CRC]

Field		      Description
TYPE	        N (Normal), R (Redirection), B (Beacon), E (Beacon Response), Q (User Search), U (User Notification)
ORIGIN	      Node ID that sent the frame
DESTINATION	  Recipient node ID (0 = Broadcast)
TTL	          Time To Live (remaining hops)
ROUTE	        List of nodes the frame has passed through
CONTENT	      Message payload or system command
CRC	          Integrity verification code (Layer 2)

Frame Types
Type	Description
N	    Normal Frame (user or system)
R	    Redirection Frame (in transit)
B	    Beacon (neighbor detection)
E	    Beacon Response
Q	    User Search in mesh
U	    User Location Notification

Layer 2 Features
  - Neighbor Detection: Periodic beacons to maintain neighbor tables
  - Error Control: CRC-16 for frame integrity
  - Flow Control: TTL to prevent infinite loops
  - Addressing: 8-bit node IDs (0-255)

⚙️ Installation and Configuration

1. Clone the repository:
    git clone https://github.com/SasakiRyo27/SisME-LoRa-Protocol-CALM
    cd SisME-LoRa-Protocol-CALM

2. Backend (Django Server - SisME)
Requirements: Python 3.10+

Create and activate a virtual environment:
    python -m venv venv
    source venv/bin/activate  # On Windows: venv\Scripts\activate
  
Install dependencies:
    pip install django
  
Configure the database:
    python manage.py migrate
  
Create a superuser for admin:
    python manage.py createsuperuser
  
Run the development server:
    python manage.py runserver 0.0.0.0:8000
  
3. Frontend (Web Interface)
Static files (CSS, JS, images) are served automatically in DEBUG mode

For production: python manage.py collectstatic

4. Firmware (Heltec Nodes with CALM)
Requirements: Arduino IDE or PlatformIO

Open any of the following .ino files:
    - heltec_master_node.ino: Gateway/Master Node (WiFi-connected, bridge between SisME and CALM network)
    - heltec_relay_node.ino: Relay/Repeater Node (only forwards CALM frames)
    - heltec_emisor.ino: Emitter Node (sends messages from the device itself)

Configure a unique DEVICE_ID for each node (1, 2, 3...).

In heltec_master_node.ino, configure WiFi credentials and Django server IP:
    - const char* wifi_ssid = "YourWiFiNetwork";
    - const char* wifi_password = "YourPassword";
    - const char* django_server = "http://x.x.x.x:8000"; // SisME server IP

Upload the code to the Heltec device.

📱 Usage
SisME (Web)
  1. Access http://localhost:8000 or your server IP
  2. Register or log in
  3. The system will automatically assign you to a LoRa node with ID format X.Y (node.sub-id)
  4. Use the general chat for Broadcast or search for users for Private Messages
  5. Monitor the CALM network status in the right side panel

LoRa Nodes (CALM)
  - Serial Monitor: Connect via USB and open the serial monitor (115200 baud)
  - Send CALM frame: Type the message in the serial console and press Enter, then enter the destination ID (0 for Broadcast)
  - OLED Display: Shows incoming messages, battery status, and detected neighbors

Special CALM Commands (via Serial)
  SYNCUSERS: Forces user list synchronization across the entire mesh

📁 Project Structure

SisME-LoRa-Protocol-CALM/
├── chat/                        # Django application
│   ├── migrations/               # Database migrations
│   ├── templates/                # HTML templates
│   │   ├── chat/
│   │   │   └── chat_room.html
│   │   └── registration/
│   │       ├── login.html
│   │       ├── register.html
│   │       ├── register_success.html
│   │       └── edit_profile.html
│   ├── static/                   # Static files
│   │   ├── css/
│   │   ├── icons/
│   │   └── js/
│   ├── admin.py                 # Admin configuration
│   ├── models.py                # Data models
│   ├── views.py                 # View logic
│   ├── urls.py                  # Application routes
│   └── ...
├── sme_lora_project/            # Django project configuration
│   ├── settings.py
│   ├── urls.py
│   └── ...
├── hardware/                    # Source code for CALM nodes
│   ├── heltec_master_node.ino   # Gateway Node
│   ├── heltec_relay_node.ino    # Relay Node
│   └── heltec_emisor.ino        # Emitter Node
└── manage.py

📡 API REST Endpoints
The backend exposes an API for LoRa nodes to communicate:

Method	  Endpoint	                  Description
POST	    /api/send-message/	        Send a message from a LoRa node to the server
GET	      /api/get-messages/	        Get pending messages for a node
POST	    /api/update-node/	          Register or update a LoRa node
PUT	      /api/update-node/<id>/	    Update a node's status
PUT	      /api/mark-delivered/<id>/	  Mark a message as delivered
GET	      /api/node-status/	          Get status of all nodes
GET	      /api/users-index/	          List all registered users
POST	    /api/sync-user/	            Sync a user on the server
POST	    /api/update-user-location/	Update a user's location

🤝 Contributing
Contributions are welcome! Please open an issue first to discuss what you would like to implement.

📜 License
This project is licensed under the GNU General Public License v3.0 - see the LICENSE file for details.

👨‍💻 Credits
Massimo Larger & Claudio Uribe
  - Firmware development and CALM protocol implementation (Layer 2)
  - Django backend development (SisME) and web interface

📧 Contact
For any questions or suggestions, feel free to contact us at:

    massimoalv.27@gmail.com/elpolodog@gmail.com

🌟 Acknowledgments

    - RadioLib library for LoRa communication
    - Django community for the excellent web framework
    - Heltec for the ESP32 development boards
    - All contributors and testers who helped improve the system

SisME + CALM: Emergency communication when you need it most! 📡🆘

SisME - Sistema de Mensajería de Emergencia
Copyright (C) 2026 Massimo Larger & Claudio Uribe

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.