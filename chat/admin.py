# SisME - Sistema de Mensajería de Emergencia
# Copyright (C) 2026  Massimo Larger & Claudio Uribe
# 
# Este programa es software libre: puedes redistribuirlo y/o modificarlo
# bajo los términos de la Licencia Pública General de GNU publicada por
# la Free Software Foundation, ya sea la versión 3 de la Licencia, o
# (a su elección) cualquier versión posterior.
# 
# Este programa se distribuye con la esperanza de que sea útil,
# pero SIN NINGUNA GARANTÍA. Consulte la Licencia Pública General
# de GNU para obtener más detalles.

from django.contrib import admin
from .models import UserProfile, Message, LoRaNode

@admin.register(UserProfile)
class UserProfileAdmin(admin.ModelAdmin):
    list_display = ['user', 'full_name', 'birth_date', 'zone']
    search_fields = ['user__username', 'full_name']

@admin.register(Message)
class MessageAdmin(admin.ModelAdmin):
    list_display = ['id', 'user', 'node_id', 'content_preview', 'timestamp', 'delivered_to_node']
    list_filter = ['delivered_to_node', 'is_emergency', 'timestamp']
    search_fields = ['content']
    
    def content_preview(self, obj):
        return obj.content[:50] + '...' if len(obj.content) > 50 else obj.content
    content_preview.short_description = 'Mensaje'

@admin.register(LoRaNode)
class LoRaNodeAdmin(admin.ModelAdmin):
    list_display = ['node_id', 'zone_name', 'battery_level', 'status', 'last_seen']
    list_filter = ['status', 'zone_name']