#!/bin/bash

if [ -f /usr/bin/gnome-terminal ] ; then 
  comando="gnome-terminal"
elif [ -f /usr/bin/mate-terminal ] ; then
  comando="mate-terminal"
elif [ -f /usr/bin/xfce4-terminal ] ; then
  comando="xfce4-terminal"
elif [ -f /usr/bin/xterm ] ; then
  comando="xterm"
fi 

gcc src/servidor_UDP.c -o bin/servidor_UDP
gcc src/servidor_WEB.c -o bin/servidor_WEB
gcc src/PS0.c -o bin/PS0
#gcc src/simular_envio_sensores_rnd.c -o bin/simular_envio_sensores_rnd
cd bin
var="$comando --geometry 90x15+0+0  -e 'bash -c \"echo Ejecutando servidor UDP;./servidor_UDP;read;\"' "
eval $var &
sleep 0.6
var="$comando --geometry 70x15+0+700 -e 'bash -c \"echo Ejecutando servidor WEB;./servidor_WEB;read;\"' "
eval $var &
sleep 0.6
var="$comando --geometry 70x10+820+350 -e 'bash -c \"echo Ejecutando simular_envio_sensores_rnd;./simular_envio_sensores_rnd;read;\"' "
eval $var &
sleep 5
xdg-open http://127.0.0.1:2017
 
