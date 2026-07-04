# SisME - Sistema de Mensajería de Emergencia
# Copyright (C) 2026  Massimo Larger & Claudio Uribe
# 
# Este programa es software libre: puedes redistribuirlo y/o modificarlo
# bajo los términos de la Licencia Pública General de GNU publicada por
# la Free Software Foundation, ya sea la versión 3 de la Licencia, o
# (a su elección) cualquier versión posterior.
# 
# Este programa se distribuye con la esperanza de que sea útil.
# Consulte la Licencia Pública General de GNU para obtener más detalles.

from django.urls import path
from . import views

urlpatterns = [
    path('', views.chat_room, name='chat_room'),
    path('api/messages/', views.get_messages, name='get_messages'),
    path('api/send/', views.send_message, name='send_message'),
    
    # Endpoints para LoRa
    path('api/send-message/', views.api_send_message, name='api_send_message'),
    path('api/get-messages/', views.api_get_messages, name='api_get_messages'),
    path('api/update-node/', views.api_update_node, name='api_update_node'),
    path('api/update-node/<int:node_id>/', views.api_update_node_detail, name='api_update_node_detail'),
    path('api/mark-delivered/<int:message_id>/', views.api_mark_delivered, name='api_mark_delivered'),
    path('api/node-status/', views.api_node_status, name='api_node_status'),
    path('api/test-lora/', views.api_test_lora, name='api_test_lora'),
    path('api/update-user-node/', views.api_update_user_node, name='api_update_user_node'),
    path('api/update-user-location/', views.api_update_user_location, name='api_update_user_location'),
    path('api/user-info/', views.api_get_user_info, name='api_get_user_info'),
    path('api/users-list/', views.get_users_list, name='get_users_list'),
    path('api/sync-user/', views.api_sync_user, name='api_sync_user'),
    path('api/private-inbox/', views.api_private_inbox, name='api_private_inbox'),
    path('api/users-index/', views.api_users_index, name='api_users_index'),
    path('api/user-export/', views.api_user_export, name='api_user_export'),
    path('profile/edit/', views.edit_profile, name='edit_profile'),
]
