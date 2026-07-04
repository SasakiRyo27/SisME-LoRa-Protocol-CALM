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

from django.db import models
from django.contrib.auth.models import User
from django.utils import timezone

# Create your models here.

class UserProfile(models.Model):
    user = models.OneToOneField(User, on_delete=models.CASCADE, related_name='profile')
    full_name = models.CharField(max_length=100, verbose_name="Nombre completo")
    birth_date = models.DateField(verbose_name="Fecha de nacimiento")
    zone = models.CharField(max_length=100, default="General", verbose_name="Zona/Área")
    
    # Campos para Unicast LoRa
    current_node_id = models.IntegerField(null=True, blank=True, verbose_name="ID del nodo actual")
    sub_id = models.IntegerField(null=True, blank=True, verbose_name="Sub-ID (ej. X.1)")
    
    def __str__(self):
        return f'Perfil de {self.user.username} ({self.current_node_id}.{self.sub_id if self.sub_id else "?"})'

class Message(models.Model):
    user = models.ForeignKey(User, on_delete=models.CASCADE, related_name='messages', null=True, blank=True)
    target_user = models.ForeignKey(User, on_delete=models.SET_NULL, related_name='received_messages', null=True, blank=True)
    content = models.TextField()
    timestamp = models.DateTimeField(auto_now_add=True)
    
    # Campos para mensajes de nodos LoRa
    node_id = models.IntegerField(null=True, blank=True, verbose_name="ID del nodo LoRa")
    zone_name = models.CharField(max_length=100, default="General", verbose_name="Zona del nodo")
    is_emergency = models.BooleanField(default=False, verbose_name="Mensaje de emergencia")
    delivered_to_node = models.BooleanField(default=False, verbose_name="Entregado a nodo")
    destination_node = models.IntegerField(null=True, blank=True, default=0, verbose_name="Nodo destino (0=broadcast)")
    destination_sub_id = models.IntegerField(null=True, blank=True, default=0, verbose_name="Sub-ID destino")

    def __str__(self):
        if self.user:
            return f'{self.user.username}: {self.content[:50]}'
        else:
            return f'Nodo {self.node_id}: {self.content[:50]}'

    class Meta:
        ordering = ['timestamp']

class LoRaNode(models.Model):
    STATUS_CHOICES = [
        ('online', 'En línea'),
        ('away', 'Inactivo'),
        ('offline', 'Desconectado'),
    ]
    
    node_id = models.IntegerField(unique=True, verbose_name="ID del nodo")
    zone_name = models.CharField(max_length=100, verbose_name="Zona/Ubicación")
    battery_level = models.IntegerField(default=100, verbose_name="Batería (%)")
    status = models.CharField(max_length=10, choices=STATUS_CHOICES, default='offline')
    last_seen = models.DateTimeField(auto_now=True, verbose_name="Última vez visto")
    neighbors_data = models.TextField(default='[]', verbose_name="Vecinos (JSON)")
    
    def __str__(self):
        return f'Nodo {self.node_id} - {self.zone_name}'
    
    class Meta:
        ordering = ['node_id']