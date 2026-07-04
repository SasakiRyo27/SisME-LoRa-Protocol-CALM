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

from django.shortcuts import render, redirect
from django.contrib.auth.decorators import login_required
from django.contrib.auth.forms import UserCreationForm, AuthenticationForm
from django.contrib.auth.models import User
from django.contrib.auth import login
from django.contrib.auth.views import LoginView
from django.core.validators import RegexValidator
from django import forms
from django.http import JsonResponse
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_http_methods
from django.utils import timezone
from django.db import models
from django.conf import settings
import json
import uuid
import re
from .models import Message, UserProfile, LoRaNode

def _require_lora_token_if_configured(request):
    token = getattr(settings, 'LORA_SYNC_TOKEN', None)
    if not token:
        return None
    header_token = request.headers.get('X-SISME-TOKEN')
    if header_token != token:
        return JsonResponse({'status': 'error', 'message': 'No autorizado'}, status=403)
    return None

def _get_active_node():
    return LoRaNode.objects.filter(status__in=['online', 'away']).order_by('-last_seen').first()

def _get_connected_node_id(request):
    node_id = request.session.get('connected_node_id')
    if node_id:
        node = LoRaNode.objects.filter(node_id=node_id).first()
        if node and node.status in ('online', 'away'):
            return node_id
    active = _get_active_node()
    if active:
        request.session['connected_node_id'] = active.node_id
        return active.node_id
    return None

def _assign_user_to_node(user, node_id):
    profile, _ = UserProfile.objects.get_or_create(
        user=user,
        defaults={
            'full_name': user.get_full_name() or user.username,
            'birth_date': timezone.now().date(),
            'zone': 'General',
        }
    )

    if not node_id:
        return profile

    if profile.current_node_id != node_id or not profile.sub_id:
        last_sub = UserProfile.objects.filter(current_node_id=node_id).aggregate(models.Max('sub_id'))['sub_id__max'] or 0
        profile.current_node_id = node_id
        profile.sub_id = last_sub + 1
        profile.save()

        Message.objects.create(
            user=None,
            content=f"USER_MOVE:{user.username}:{node_id}.{profile.sub_id}",
            zone_name="Sistema",
            destination_node=0,
            destination_sub_id=0,
            is_emergency=False,
            delivered_to_node=False,
            target_user=None,
        )

    return profile

# ============ FORMULARIOS PERSONALIZADOS ============

class CustomLoginForm(AuthenticationForm):
    username = forms.CharField(
        label="Nombre de usuario",
        max_length=30,
        widget=forms.TextInput(attrs={'autofocus': True, 'class': 'form-control'}),
        validators=[
            RegexValidator(
                regex=r'^[\w.@+\- ]+$',
                message="El nombre de usuario solo puede contener letras, números, espacios y los caracteres @/./+/-/_.",
                code='invalid_username'
            )
        ]
    )
    
    password = forms.CharField(
        label="Contraseña",
        widget=forms.PasswordInput(attrs={'class': 'form-control'})
    )

class CustomLoginView(LoginView):
    authentication_form = CustomLoginForm

    def form_valid(self, form):
        response = super().form_valid(form)
        user = self.request.user

        active = _get_active_node()
        if active:
            self.request.session['connected_node_id'] = active.node_id
        node_id = _get_connected_node_id(self.request)
        _assign_user_to_node(user, node_id)

        return response

class CustomUserCreationForm(UserCreationForm):
    full_name = forms.CharField(
        label="Nombre completo",
        max_length=100,
        widget=forms.TextInput(attrs={'class': 'form-control'})
    )
    birth_date = forms.DateField(
        label="Fecha de nacimiento",
        widget=forms.DateInput(format='%Y-%m-%d', attrs={'type': 'date', 'class': 'form-control'})
    )
    zone = forms.CharField(
        label="Zona/Área",
        max_length=100,
        required=False,
        initial="General",
        widget=forms.TextInput(attrs={'class': 'form-control', 'readonly': 'readonly'})
    )
    username = forms.CharField(
        label="Nombre de usuario",
        max_length=30,
        widget=forms.TextInput(attrs={'class': 'form-control'}),
        help_text="Máximo 30 caracteres.",
        validators=[
            RegexValidator(
                regex=r'^[\w.@+\- ]+$',
                message="El nombre de usuario solo puede contener letras, números, espacios y los caracteres @/./+/-/_.",
                code='invalid_username'
            )
        ]
    )
    
    email = forms.EmailField(
        label="Correo electrónico",
        required=True,
        widget=forms.EmailInput(attrs={'class': 'form-control'})
    )

    class Meta(UserCreationForm.Meta):
        model = User
        fields = ("username", "email", "full_name", "birth_date", "zone")

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.fields['password1'].widget = forms.PasswordInput(attrs={'class': 'form-control'})
        self.fields['password2'].widget = forms.PasswordInput(attrs={'class': 'form-control'})
        
        password_help = """
        <ul class='ps-3 mb-0 text-muted' style='font-size: 0.85rem;'>
            <li>Usa al menos 8 caracteres.</li>
            <li>No uses una contraseña muy común.</li>
            <li>No uses solo números.</li>
        </ul>
        """
        self.fields['password1'].help_text = password_help

    def clean_email(self):
        email = self.cleaned_data.get('email')
        if User.objects.filter(email=email).exists():
            raise forms.ValidationError("Este correo electrónico ya está registrado.")
        return email

# ============ VISTAS DEL CHAT ============

@login_required
def get_messages(request):
    """Obtiene mensajes filtrados para el chat web (General o Privado)"""
    target_username = request.GET.get('chat_with')
    
    if target_username:
        # Si hay un target, usamos la lógica de chat privado
        try:
            other_user = User.objects.get(username=target_username)
            messages = Message.objects.filter(
                models.Q(target_user__isnull=False),
                (models.Q(user=request.user, target_user=other_user) | 
                 models.Q(user=other_user, target_user=request.user))
            ).order_by('timestamp')
        except User.DoesNotExist:
            return JsonResponse({'status': 'error', 'message': 'Usuario no encontrado'}, status=404)
    else:
        # Chat general (Broadcast) - Solo mensajes públicos (sin target_user)
        messages = Message.objects.filter(
            target_user__isnull=True,
            destination_node=0,
            destination_sub_id=0
        ).exclude(
            models.Q(content__startswith="USER_MOVE:") |
            models.Q(content__startswith="USER_CREATE|") |
            models.Q(content__startswith="USER_CREATE_CHUNK|")
        ).order_by('timestamp')

    message_list = []
    for msg in messages:
        if msg.user:
            username = msg.user.username
        else:
            username = f"Nodo {msg.node_id}" if msg.node_id else "Sistema"
            
        message_list.append({
            'username': username,
            'sender': username,
            'content': msg.content,
            'timestamp': timezone.localtime(msg.timestamp).strftime("%d/%m/%Y %H:%M:%S"),
            'zone': msg.zone_name,
            'is_emergency': msg.is_emergency
        })
    return JsonResponse({'messages': message_list})

@login_required
@require_http_methods(["GET"])
def api_private_inbox(request):
    after_id_raw = request.GET.get('after_id') or '0'
    try:
        after_id = int(after_id_raw)
    except ValueError:
        after_id = 0

    messages = Message.objects.filter(
        target_user=request.user,
        id__gt=after_id
    ).order_by('id')[:100]

    payload = []
    for msg in messages:
        if msg.user:
            sender_username = msg.user.username
            sender_full_name = getattr(getattr(msg.user, 'profile', None), 'full_name', None) or sender_username
        else:
            sender_username = f"Nodo {msg.node_id}" if msg.node_id else "Sistema"
            sender_full_name = sender_username

        ts = timezone.localtime(msg.timestamp)
        payload.append({
            'id': msg.id,
            'sender_username': sender_username,
            'sender_full_name': sender_full_name,
            'content': msg.content,
            'timestamp': ts.strftime("%d/%m/%Y %H:%M:%S"),
            'ts_ms': int(ts.timestamp() * 1000),
        })

    return JsonResponse({'messages': payload})

@require_http_methods(["GET"])
def api_users_index(request):
    auth = _require_lora_token_if_configured(request)
    if auth:
        return auth
    users = User.objects.all().values_list('username', flat=True).order_by('username')
    return JsonResponse({'users': list(users)})

@require_http_methods(["GET"])
def api_user_export(request):
    auth = _require_lora_token_if_configured(request)
    if auth:
        return auth
    username = (request.GET.get('username') or '').strip()
    if not username:
        return JsonResponse({'status': 'error', 'message': 'Falta username'}, status=400)
    user = User.objects.filter(username=username).first()
    if not user:
        return JsonResponse({'status': 'error', 'message': 'Usuario no encontrado'}, status=404)
    return JsonResponse({
        'status': 'ok',
        'u': user.username,
        'p': user.password,
        'e': user.email or "",
    })

@login_required
@require_http_methods(["POST"])
def send_message(request):
    """Envía un mensaje desde la web con ruteo inteligente"""
    try:
        content = request.POST.get('content')
        target_username = request.POST.get('target_user')
        is_emergency = False
        
        target_user = None
        dest_node = 0
        dest_sub = 0
        route_missing = False
        
        # Si se especifica un usuario, es Unicast
        if target_username and target_username != 'broadcast':
            target_user = User.objects.get(username=target_username)
            # Acceso seguro al perfil del destinatario
            target_profile = getattr(target_user, 'profile', None)
            if (
                (not target_profile) or
                (not getattr(target_profile, 'current_node_id', None)) or
                (not getattr(target_profile, 'sub_id', None))
            ):
                route_missing = True
                dest_node = 0
                dest_sub = 0
            else:
                dest_node = int(target_profile.current_node_id)
                dest_sub = int(target_profile.sub_id)
        
        # Crear mensaje con los campos de ruteo correctos
        msg = Message.objects.create(
            user=request.user,
            target_user=target_user, # Clave para la separación de historiales
            content=content,
            is_emergency=is_emergency,
            destination_node=dest_node,
            destination_sub_id=dest_sub,
            zone_name=getattr(request.user.profile, 'zone', 'Web') if hasattr(request.user, 'profile') else "Web",
            delivered_to_node=False
        )
        
        return JsonResponse({
            'status': 'ok',
            'username': msg.user.username,
            'content': msg.content,
            'timestamp': timezone.localtime(msg.timestamp).strftime("%d/%m/%Y %H:%M"),
            'route_missing': route_missing
        })
    except Exception as e:
        return JsonResponse({'status': 'error', 'message': str(e)}, status=400)

@login_required
def chat_room(request):
    """Vista principal del chat"""
    node_id = _get_connected_node_id(request)
    profile = _assign_user_to_node(request.user, node_id)

    if request.method == 'POST':
        content = request.POST.get('content')
        if content:
            zone = profile.zone
            Message.objects.create(
                user=request.user,
                content=content,
                zone_name=zone,
                destination_node=0,  # Broadcast
                is_emergency=False,
                target_user=None    # Asegurar que es broadcast
            )
            return redirect('chat_room')
    
    # Solo cargar mensajes broadcast para la vista inicial
    messages = Message.objects.filter(
        target_user__isnull=True,
        destination_node=0,
        destination_sub_id=0
    ).exclude(
        models.Q(content__startswith="USER_MOVE:") |
        models.Q(content__startswith="USER_CREATE|") |
        models.Q(content__startswith="USER_CREATE_CHUNK|")
    ).order_by('timestamp')
    active_nodes = LoRaNode.objects.filter(status__in=['online', 'away'])[:10]
    
    context = {
        'chat_messages': messages,
        'active_nodes': active_nodes,
        'user_zone': profile.zone,
        'active_node_id': node_id,
        'user_full_id': f"{node_id}.{profile.sub_id}" if node_id and profile.sub_id else "Sin nodo"
    }
    return render(request, 'chat/chat_room.html', context)

# ============ VISTAS DE AUTENTICACIÓN ============

def register(request):
    if request.method == 'POST':
        form = CustomUserCreationForm(request.POST)
        if form.is_valid():
            user = form.save()
            user_zone = form.cleaned_data.get('zone', 'General')
            
            # --- ASIGNACIÓN INTELIGENTE DE NODO (Cercanía por Zona) ---
            # 1. Intentamos buscar un nodo online que coincida exactamente con la zona del usuario
            target_node = LoRaNode.objects.filter(
                zone_name__icontains=user_zone, 
                status='online'
            ).order_by('-last_seen').first()
            
            # 2. Si no hay coincidencia por zona, buscamos cualquier nodo online disponible
            if not target_node:
                target_node = LoRaNode.objects.filter(status='online').order_by('-last_seen').first()
                
            # 3. Si no hay nada online, buscamos el último nodo visto (aunque esté inactivo)
            if not target_node:
                target_node = LoRaNode.objects.all().order_by('-last_seen').first()
            
            # 4. Fallback final: Si la BD está vacía de nodos, usamos el ID 2 (Master)
            target_node_id = target_node.node_id if target_node else 2
            
            # Calcular el siguiente sub_id disponible para ese nodo específico
            last_sub = UserProfile.objects.filter(current_node_id=target_node_id).aggregate(models.Max('sub_id'))['sub_id__max'] or 0
            new_sub_id = last_sub + 1
            
            # Crear el perfil con los datos de ruteo
            profile = UserProfile.objects.create(
                user=user,
                full_name=form.cleaned_data.get('full_name'),
                birth_date=form.cleaned_data.get('birth_date'),
                zone=user_zone,
                current_node_id=target_node_id,
                sub_id=new_sub_id
            )
            
            payload = {
                'u': user.username,
                'p': user.password,
                'e': user.email or "",
            }
            payload_json = json.dumps(payload, separators=(',', ':'))
            sync_id = uuid.uuid4().hex[:8]
            chunk_size = 45
            chunks = [payload_json[i:i + chunk_size] for i in range(0, len(payload_json), chunk_size)]
            total = len(chunks)
            for idx, chunk in enumerate(chunks, start=1):
                Message.objects.create(
                    user=None,
                    content=f"USER_CREATE_CHUNK|{sync_id}|{idx}/{total}|{chunk}",
                    zone_name="Sistema",
                    destination_node=0,
                    destination_sub_id=0,
                    is_emergency=False,
                    delivered_to_node=False,
                    target_user=None,
                )

            # Notificar a la malla LoRa sobre el nuevo usuario
            Message.objects.create(
                user=None,
                content=f"USER_MOVE:{user.username}:{target_node_id}.{new_sub_id}",
                zone_name="Sistema",
                destination_node=0, # Broadcast a todos los nodos
                is_emergency=False,
                delivered_to_node=False
            )
            
            print(f"[REGISTRO] Nuevo usuario {user.username} asignado a {target_node_id}.{new_sub_id}")
            return redirect('register_success')
    else:
        form = CustomUserCreationForm()
    return render(request, 'registration/register.html', {'form': form})

def register_success(request):
    return render(request, 'registration/register_success.html')

# ============ API PARA NODOS LoRa ============

@csrf_exempt
@require_http_methods(["POST"])
def api_sync_user(request):
    """Sincroniza un usuario recibido por LoRa a este servidor (sin internet)."""
    auth = _require_lora_token_if_configured(request)
    if auth:
        return auth
    try:
        data = json.loads(request.body.decode('utf-8'))
        username = (data.get('u') or data.get('username') or '').strip()
        password_hash = data.get('p') or data.get('password') or ''
        email = (data.get('e') or data.get('email') or '').strip()
        full_name = (data.get('full_name') or '').strip() or username
        birth_date = data.get('birth_date')
        zone = (data.get('zone') or 'General').strip() or 'General'
        current_node_id = data.get('current_node_id')
        sub_id = data.get('sub_id')

        if not username:
            return JsonResponse({'status': 'error', 'message': 'Falta username'}, status=400)

        allowed_prefixes = ('pbkdf2_', 'argon2$', 'bcrypt$', 'scrypt$')
        if password_hash and not password_hash.startswith(allowed_prefixes):
            return JsonResponse({'status': 'error', 'message': 'Hash de contraseña inválido'}, status=400)

        user, created = User.objects.get_or_create(username=username)
        if email and not User.objects.exclude(username=username).filter(email=email).exists():
            user.email = email
        if password_hash:
            user.password = password_hash
        user.save()

        profile, _ = UserProfile.objects.get_or_create(
            user=user,
            defaults={
                'full_name': full_name,
                'birth_date': timezone.now().date(),
                'zone': 'General',
            }
        )

        profile.full_name = full_name
        if birth_date:
            try:
                if isinstance(birth_date, str):
                    profile.birth_date = timezone.datetime.fromisoformat(birth_date).date()
            except Exception:
                pass
        profile.zone = 'General'
        if current_node_id is not None:
            try:
                profile.current_node_id = int(current_node_id)
            except Exception:
                pass
        if sub_id is not None:
            try:
                profile.sub_id = int(sub_id)
            except Exception:
                pass
        profile.save()

        return JsonResponse({'status': 'ok', 'created': created})
    except Exception as e:
        return JsonResponse({'status': 'error', 'message': str(e)}, status=400)

@csrf_exempt
@require_http_methods(["POST"])
def api_send_message(request):
    """Recibe mensajes desde los nodos LoRa y los guarda en la BD"""
    auth = _require_lora_token_if_configured(request)
    if auth:
        return auth
    try:
        data = json.loads(request.body.decode('utf-8'))
        node_id = data.get('node_id')
        content = data.get('content')
        zone_name = data.get('zone_name', 'General')
        destination_node = data.get('destino_id', 0)
        destination_sub_id = data.get('destino_sub_id', 0)
        sender_username = data.get('sender')

        sender_username_str = (sender_username or "").strip()
        sender_user = None
        if sender_username_str:
            sender_user = User.objects.filter(username=sender_username_str).first()

        # Emergencia SOLO si el mensaje proviene del nodo (ej: "ID1", "Nodo 1") o si no hay remitente.
        # Si el remitente es un usuario (aunque venga por LoRa), NO debe marcarse como emergencia.
        sender_looks_like_node = bool(re.match(r'^(ID\d+|Nodo\s*\d+)$', sender_username_str, flags=re.IGNORECASE))
        is_emergency = (not sender_username_str) or sender_looks_like_node
        if sender_user:
            is_emergency = False

        if isinstance(content, str) and (
            content.startswith("USER_MOVE:") or
            content.startswith("USER_CREATE|") or
            content.startswith("USER_CREATE_CHUNK|")
        ):
            is_emergency = False
        
        print(f"[API] Mensaje recibido de nodo {node_id} para nodo {destination_node}.{destination_sub_id}: {content}")
        
        target_user = None
        # Si el destino es el Master Node (usualmente ID 1 o 2, configurado en .ino)
        # y tiene un sub_id, buscamos al usuario web correspondiente
        if destination_node != 0 and destination_sub_id != 0:
            try:
                profile = UserProfile.objects.get(current_node_id=destination_node, sub_id=destination_sub_id)
                target_user = profile.user
            except UserProfile.DoesNotExist:
                pass
        
        # Crear mensaje en la base de datos
        message = Message.objects.create(
            user=sender_user,
            target_user=target_user,
            content=content,
            node_id=node_id,
            zone_name=zone_name,
            is_emergency=is_emergency,
            destination_node=destination_node,
            destination_sub_id=destination_sub_id,
            delivered_to_node=True
        )
        
        # Opcional: Actualizar batería del nodo si viene en el JSON
        if 'battery_level' in data:
            LoRaNode.objects.filter(node_id=node_id).update(
                battery_level=data['battery_level'],
                last_seen=timezone.now(),
                status='online'
            )
        
        return JsonResponse({'status': 'ok', 'message_id': message.id})
    except Exception as e:
        print(f"[API] Error: {str(e)}")
        return JsonResponse({'status': 'error', 'message': str(e)}, status=400)

@require_http_methods(["GET"])
def api_get_messages(request):
    """Retorna mensajes para un nodo o conversación específica con separación absoluta"""
    auth = _require_lora_token_if_configured(request)
    if auth:
        return auth
    node_id = request.GET.get('node_id')
    target_username = request.GET.get('chat_with')
    
    if node_id:
        # El Master Node (Gateway) debe obtener TODOS los mensajes que aún no han sido
        # enviados a la malla LoRa, sin importar el nodo destino.
        messages = Message.objects.filter(
            delivered_to_node=False
        ).exclude(
            content__startswith="USER_CREATE|"
        ).filter(
            models.Q(
                target_user__isnull=True,
                destination_node=0,
                destination_sub_id=0
            ) | models.Q(
                destination_node__gt=0,
                destination_sub_id__gt=0
            )
        ).order_by('timestamp')
    elif target_username:
        # Mensajes para el widget web (Historial privado estricto)
        try:
            other_user = User.objects.get(username=target_username)
            # FILTRO ESTRICTO: Solo mensajes con target_user definido entre A y B
            messages = Message.objects.filter(
                models.Q(target_user__isnull=False),
                (models.Q(user=request.user, target_user=other_user) | 
                 models.Q(user=other_user, target_user=request.user))
            ).order_by('timestamp')
        except User.DoesNotExist:
            return JsonResponse({'status': 'error', 'message': 'Usuario no encontrado'}, status=404)
    else:
        # Chat general (Broadcast) - Solo mensajes públicos (sin target_user)
        messages = Message.objects.filter(
            target_user__isnull=True,
            destination_node=0,
            destination_sub_id=0
        ).exclude(
            models.Q(content__startswith="USER_MOVE:") |
            models.Q(content__startswith="USER_CREATE|") |
            models.Q(content__startswith="USER_CREATE_CHUNK|")
        ).order_by('timestamp')

    message_list = []
    for msg in messages:
        sender_name = msg.user.username if msg.user else "Sistema"
        message_list.append({
            'id': msg.id,
            'content': msg.content,
            'timestamp': timezone.localtime(msg.timestamp).strftime("%d/%m/%Y %H:%M"),
            'username': sender_name,
            'sender': sender_name,
            'destino_id': msg.destination_node,
            'destino_sub_id': msg.destination_sub_id,
            'is_emergency': msg.is_emergency
        })
    
    return JsonResponse({'messages': message_list})

@csrf_exempt
@require_http_methods(["POST"])
def api_update_node(request):
    """Registra un nuevo nodo LoRa"""
    auth = _require_lora_token_if_configured(request)
    if auth:
        return auth
    try:
        data = json.loads(request.body.decode('utf-8'))
        node_id = data.get('node_id')
        zone_name = data.get('zone_name')
        battery_level = data.get('battery_level', 100)
        status = data.get('status', 'online')
        
        node, created = LoRaNode.objects.update_or_create(
            node_id=node_id,
            defaults={
                'zone_name': zone_name,
                'battery_level': battery_level,
                'status': status,
                'last_seen': timezone.now()
            }
        )
        
        print(f"[API] Nodo {node_id} registrado (nuevo: {created})")
        return JsonResponse({'status': 'ok', 'created': created})
    except Exception as e:
        return JsonResponse({'status': 'error', 'message': str(e)}, status=400)

@csrf_exempt
@require_http_methods(["PUT"])
def api_update_node_detail(request, node_id):
    """Actualiza estado detallado de un nodo"""
    auth = _require_lora_token_if_configured(request)
    if auth:
        return auth
    try:
        data = json.loads(request.body.decode('utf-8'))
        node = LoRaNode.objects.get(node_id=node_id)
        
        if 'battery_level' in data:
            node.battery_level = data['battery_level']
        if 'status' in data:
            node.status = data['status']
        
        node.last_seen = timezone.now()
        
        if 'neighbors' in data:
            node.neighbors_data = json.dumps(data['neighbors'])
        
        node.save()
        
        print(f"[API] Nodo {node_id} actualizado - Estado: {node.status}, Batería: {node.battery_level}%")
        
        return JsonResponse({
            'status': 'ok',
            'node_id': node_id,
            'node_status': node.status,
            'last_seen': node.last_seen.isoformat()
        })
        
    except LoRaNode.DoesNotExist:
        return JsonResponse({'status': 'error', 'message': 'Node not found'}, status=404)
    except Exception as e:
        return JsonResponse({'status': 'error', 'message': str(e)}, status=400)

@csrf_exempt
@require_http_methods(["PUT"])
def api_mark_delivered(request, message_id):
    """Marca un mensaje como entregado a un nodo"""
    auth = _require_lora_token_if_configured(request)
    if auth:
        return auth
    try:
        data = json.loads(request.body.decode('utf-8'))
        node_id = data.get('node_id')
        
        message = Message.objects.get(id=message_id)
        message.delivered_to_node = True
        message.save()
        
        print(f"[API] Mensaje {message_id} marcado como entregado al nodo {node_id}")
        return JsonResponse({'status': 'ok'})
    except Message.DoesNotExist:
        return JsonResponse({'status': 'error', 'message': 'Message not found'}, status=404)
    except Exception as e:
        return JsonResponse({'status': 'error', 'message': str(e)}, status=400)

@login_required
@require_http_methods(["POST"])
def api_update_user_node(request):
    """Actualiza el nodo y sub_id de un usuario (Unicast)"""
    try:
        data = json.loads(request.body.decode('utf-8'))
        node_id = data.get('node_id')
        username = data.get('username') or request.user.username
        if username != request.user.username:
            return JsonResponse({'status': 'error', 'message': 'Acceso denegado'}, status=403)

        if node_id is None:
            return JsonResponse({'status': 'error', 'message': 'node_id requerido'}, status=400)

        request.session['connected_node_id'] = node_id
        profile = _assign_user_to_node(request.user, node_id)
        
        # Si el nodo cambió, avisar a la red (notificación broadcast de cambio)
        return JsonResponse({
            'status': 'ok', 
            'sub_id': profile.sub_id,
            'full_id': f"{node_id}.{profile.sub_id}"
        })
    except Exception as e:
        return JsonResponse({'status': 'error', 'message': str(e)}, status=400)

@csrf_exempt
@require_http_methods(["POST"])
def api_update_user_location(request):
    try:
        token = getattr(settings, 'LORA_SYNC_TOKEN', None)
        if token:
            header_token = request.headers.get('X-SISME-TOKEN')
            if header_token != token:
                return JsonResponse({'status': 'error', 'message': 'No autorizado'}, status=403)

        data = json.loads(request.body.decode('utf-8'))
        username = (data.get('username') or '').strip()
        node_id = data.get('node_id')
        sub_id = data.get('sub_id')

        if not username:
            return JsonResponse({'status': 'error', 'message': 'username requerido'}, status=400)
        if node_id is None or sub_id is None:
            return JsonResponse({'status': 'error', 'message': 'node_id y sub_id requeridos'}, status=400)

        try:
            node_id = int(node_id)
            sub_id = int(sub_id)
        except (TypeError, ValueError):
            return JsonResponse({'status': 'error', 'message': 'node_id/sub_id inválidos'}, status=400)

        user = User.objects.filter(username=username).first()
        if not user:
            return JsonResponse({'status': 'error', 'message': 'Usuario no encontrado'}, status=404)

        profile, _ = UserProfile.objects.get_or_create(
            user=user,
            defaults={
                'full_name': user.get_full_name() or user.username,
                'birth_date': timezone.now().date(),
                'zone': 'General',
            }
        )

        profile.current_node_id = node_id
        profile.sub_id = sub_id
        profile.save(update_fields=['current_node_id', 'sub_id'])

        Message.objects.filter(
            target_user=user,
            delivered_to_node=False
        ).update(
            destination_node=node_id,
            destination_sub_id=sub_id
        )

        Message.objects.filter(
            target_user__isnull=True,
            delivered_to_node=True,
            destination_node=node_id,
            destination_sub_id=sub_id
        ).update(target_user=user)

        return JsonResponse({'status': 'ok', 'full_id': f"{node_id}.{sub_id}"})
    except Exception as e:
        return JsonResponse({'status': 'error', 'message': str(e)}, status=400)

@login_required
@require_http_methods(["GET"])
def api_get_user_info(request):
    """Busca la ubicación (nodo.sub_id) de un usuario"""
    username = request.GET.get('username')
    try:
        user = User.objects.get(username=username)
        profile = user.profile
        return JsonResponse({
            'status': 'ok',
            'username': username,
            'node_id': profile.current_node_id,
            'sub_id': profile.sub_id,
            'full_id': f"{profile.current_node_id}.{profile.sub_id}" if profile.current_node_id else "unknown"
        })
    except User.DoesNotExist:
        return JsonResponse({'status': 'error', 'message': 'Usuario no encontrado'}, status=404)

@require_http_methods(["GET"])
def api_node_status(request):
    """Retorna el estado de todos los nodos para el panel web"""
    threshold_away = timezone.now() - timezone.timedelta(minutes=3)
    threshold_offline = timezone.now() - timezone.timedelta(minutes=10)
    
    LoRaNode.objects.filter(last_seen__lt=threshold_offline, status__in=['online', 'away']).update(status='offline')
    LoRaNode.objects.filter(last_seen__lt=threshold_away, last_seen__gte=threshold_offline, status='online').update(status='away')

    nodes = LoRaNode.objects.all().order_by('-last_seen').values('node_id', 'zone_name', 'battery_level', 'status', 'last_seen')
    node_list = []
    active_count = 0
    
    for node in nodes:
        last_seen_time = node['last_seen']
        if not last_seen_time:
            continue
        is_online = last_seen_time > threshold_away
        
        status = node['status']
        if not is_online:
            if last_seen_time < threshold_offline:
                status = 'offline'
            else:
                status = 'away'
        
        if status == 'offline':
            continue

        if status == 'online':
            active_count += 1

        node_list.append({
            'id': node['node_id'],
            'zone': node['zone_name'],
            'battery': node['battery_level'],
            'status': status,
            'last_seen': last_seen_time.strftime("%H:%M:%S") if last_seen_time else 'Nunca'
        })
    
    # Mensajes hoy
    messages_today = Message.objects.filter(timestamp__date=timezone.now().date()).count()
    
    return JsonResponse({
        'nodes': node_list,
        'active_count': active_count,
        'messages_today': messages_today
    })

class EditProfileForm(forms.ModelForm):
    email = forms.EmailField(
        label="Correo electrónico",
        widget=forms.EmailInput(attrs={'class': 'form-control'})
    )
    zone = forms.CharField(
        label="Zona/Área",
        initial="General",
        widget=forms.TextInput(attrs={'class': 'form-control', 'readonly': 'readonly'})
    )
    birth_date = forms.DateField(
        label="Fecha de nacimiento",
        widget=forms.DateInput(format='%Y-%m-%d', attrs={'type': 'date', 'class': 'form-control'})
    )

    class Meta:
        model = UserProfile
        fields = ['full_name', 'birth_date', 'zone']
        widgets = {
            'full_name': forms.TextInput(attrs={'class': 'form-control'}),
        }

    def __init__(self, *args, **kwargs):
        user = kwargs.pop('user', None)
        super().__init__(*args, **kwargs)
        if user:
            self.fields['email'].initial = user.email

@login_required
def edit_profile(request):
    """Vista para editar el perfil del usuario"""
    # Usar get_or_create para evitar el error "User has no profile"
    profile, created = UserProfile.objects.get_or_create(
        user=request.user,
        defaults={
            'full_name': request.user.get_full_name() or request.user.username,
            'birth_date': timezone.now().date(),
            'zone': 'General'
        }
    )
    
    # Si se acaba de crear, asignar un nodo/sub_id automáticamente
    if created:
        target_node = LoRaNode.objects.filter(status='online').order_by('-last_seen').first()
        target_node_id = target_node.node_id if target_node else 2
        last_sub = UserProfile.objects.filter(current_node_id=target_node_id).aggregate(models.Max('sub_id'))['sub_id__max'] or 0
        profile.current_node_id = target_node_id
        profile.sub_id = last_sub + 1
        profile.save()

    if request.method == 'POST':
        form = EditProfileForm(request.POST, instance=profile, user=request.user)
        if form.is_valid():
            # Actualizar email del objeto User
            request.user.email = form.cleaned_data['email']
            request.user.save()
            # Guardar el perfil
            form.save()
            return redirect('chat_room')
    else:
        form = EditProfileForm(instance=profile, user=request.user)
        
    return render(request, 'registration/edit_profile.html', {
        'form': form,
        'profile': profile
    })

@login_required
def get_users_list(request):
    """Busca usuarios por nombre para el chat unicast"""
    query = request.GET.get('q', '').strip()
    if not query:
        return JsonResponse({'users': []}) # No mostrar a nadie si no hay búsqueda
        
    users = User.objects.filter(
        models.Q(username__icontains=query) | 
        models.Q(profile__full_name__icontains=query)
    ).exclude(id=request.user.id).values(
        'username', 'profile__full_name', 'profile__current_node_id', 'profile__sub_id'
    )[:5] # Limitar a 5 resultados
    
    user_list = []
    for u in users:
        nid = u['profile__current_node_id']
        sid = u['profile__sub_id']
        user_list.append({
            'username': u['username'],
            'full_name': u['profile__full_name'],
            'lora_id': f"{nid}.{sid}" if nid and sid else "Sin nodo"
        })
    return JsonResponse({'users': user_list})

@login_required
def api_test_lora(request):
    """Envía un mensaje de prueba a la red LoRa"""
    try:
        msg = Message.objects.create(
            user=request.user,
            content="-- PRUEBA DE CONEXIÓN --",
            zone_name="Sistema",
            destination_node=0,
            is_emergency=False,
            delivered_to_node=False
        )
        return JsonResponse({'status': 'ok', 'message': 'Se ha enviado un mensaje de prueba a la red.'})
    except Exception as e:
        return JsonResponse({'status': 'error', 'message': str(e)}, status=400)
