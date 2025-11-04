# PASOS A SEGUIR:

1) Ingresar a la página web [https://git-scm.com/install/](https://git-scm.com/install/) y seleccionar el sistema operativo que se esta utilizando en este momento
2) Descargar e instalar el software Git para el dispositivo actual
3) Ingresar a la página web [https://code.visualstudio.com/Download](https://code.visualstudio.com/Download) y seleccionar el sistema operativo que se esta utilizando en este momento
4) Descargar e instalar el software Visual Studio Code para el dispositivo actual
5) Elegir un directorio donde se quiera instalar el proyecto
6) Abrir terminal que apunte a dicho directorio (ejecutar:  cd /RUTA/DE/SU/DIRECTORIO)
7) Ejecutar el comando:

    `git clone https://github.com/ChrisX4Ever/proyecto1/`

8) En la raíz de la carpeta creada, borrar la carpeta "_build_", si existe (**PRECAUCIÓN: NO BORRAR "main/build"**)
9) Dirigirse al dispositivo Raspberry Pi
10) Configurar la Dirección IP en los archivos _servidor_puente.py_ e _interfaz_sensor.py_
11) Abrir la carpeta _PROYECTO_1_, ir al apartado "_Tools_" y seleccionar "_Open Current Folder in Terminal_"
12) En la terminal, escribir "_python3 servidor_puente.py_", deberá mencionar que la conexión fue exitosa
13) Dirigirse al Computador
14) Abrir el archivo "_main/blink_example_main.c_" utilizando Visual Studio Code
15) Dirigirse a las lineas 39 y 40 y editar los campos "_WIFI_SSID_" y "_WIFI_PASS_" para conectarse a una red local
16) Dirigirse a la lineas 41 y 42 y editar los campos "_SERVER_IP_" y "_SERVER_PORT_" que corresponden a la Dirección IP y el Puerto de la Raspberry Pi
17) Conectar el dispositivo ESP32-S3 via USB
18) Seleccionar en la esquina inferior izquierda de la interfaz de Visual Studio Code, el botón "_Método de Flasheo_" (el ícono de "estrella") y seleccione _USB-JTAG_
19) Seleccionar en la esquina inferior izquierda de la interfaz de Visual Studio Code, el botón "_Seleccione el Chip a utilizar_" (el ícono de "microchip") y seleccione "_esp32-s3_"
20) Seleccionar en la esquina inferior izquierda de la interfaz de Visual Studio Code, el botón "_Seleccione el puerto a utilizar_" (el ícono de "enchufe") y seleccione la opción que corresponde al USB del dispositivo ESP32-S3
21) Seleccionar en la esquina inferior izquierda de la interfaz de Visual Studio Code, el botón "_Compilar, Flashear y Monitorear_" (el ícono de "fuego") para configurar el dispositivo ESP32-S3 con el código actual
22) Dirigirse al dispositivo Raspberry Pi
23) Abrir en Thonny el código _interfaz_sensor.py_, luego ejecutar
24) Seleccionar frecuencia y apresionar "_Iniciar Captura_" o el botón físico
