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
from django.urls import path, include
from django.contrib.auth import views as auth_views
from django.conf import settings
from django.conf.urls.static import static
from chat.views import register, register_success, CustomLoginView

urlpatterns = [
    path('admin/', admin.site.urls),
    path('accounts/login/', CustomLoginView.as_view(), name='login'),
    path('accounts/', include('django.contrib.auth.urls')),
    path('accounts/register/', register, name='register'),
    path('accounts/register-success/', register_success, name='register_success'),
    path('', include('chat.urls')),
]

# Servir archivos estáticos en desarrollo (incluyendo sin internet)
if settings.DEBUG:
    urlpatterns += static(settings.STATIC_URL, document_root=settings.STATIC_ROOT)
    urlpatterns += static(settings.STATIC_URL, document_root=settings.STATICFILES_DIRS[0])
