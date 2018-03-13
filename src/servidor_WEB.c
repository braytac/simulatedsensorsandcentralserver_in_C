#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <sys/shm.h> // shm
#include <sys/ipc.h>
#include <sys/sem.h>
#include <fcntl.h>
#include <time.h>

/*   DEFINICIONES PARA SEMAFOROS   */
#define NUM_SEM                       4     // Numero de semaforos
#define sem_swap_ready                1
#define sem_mem_be_reading            2
#define sem_backup_in_progress        3
#define ENTRAR(OP,n) ({(OP).sem_op = -1;(OP).sem_num = n;})//Define entrar al semaforo n
#define SALIR(OP,n)  ({(OP).sem_op = +1;(OP).sem_num = n;})//Define salir al semaforo n
#define VERIFICAR(OP,n) ({(OP).sem_op = +0;(OP).sem_num = n;}) //Para verificar si esta en verde o rojo

/* DEFINIONES PARA SEMAFOROS    */
key_t clave_sem;
int id_sem;
struct sembuf Operacion;

/* FIN  DEFINIONES PARA SEMAFOROS    */

/*  DEFINICIONES GENERALES    */
#define BUFFER_LEN 64 // tamaño en datos del buffer ping-pong
#define NRO_SENSORES 256
#define CONEXIONES_MAX 1000
#define BYTES 1024
#define LONG_MAX_LINEA 600
#define MAX_HTML 99999
#define MAX_MSG 99999

#define FTOK_A    1
#define FTOK_B    769
#define FTOK_AUX  2531
#define FTOK_SEM  7945

#pragma pack(1)  // exact fit - no padding, 1, 2, 4, 8...

struct lectura {
unsigned char ID; /* Identificación de sensor 0 – 255 */
signed int temperatura; /* Valor de temperatura multiplicado por 10 */
unsigned char RH; /* Valor de Humedad Relativa Ambiente */
};

#pragma pack() // deja alineación como se le canta a gcc

struct datos {
struct lectura lectura_sensor ;
unsigned int tiempo; /* fecha unix lectura */
};


struct config{
key_t escritura;
key_t lectura;
int contador;
};

struct datos* memoria_lectura; // para mantener la que actualmente es lectura

/*--- Memoria compartida ---*/
  key_t clave_A, clave_B, clave_aux;
	int id_memoria_A, id_memoria_B, id_memoria_aux;
	struct datos* memoria_A = NULL;
	struct datos* memoria_B = NULL;
  struct config* memoria_aux = NULL;
  struct datos data;
/*--- /Memoria compartida ---*/

/*--- BACKUPS ---*/
FILE *fd_temporal, *fd_backup;
int listenfd, clientes[CONEXIONES_MAX];
int hacer_backup;
pid_t pid_padre,pid_backupeador;
/*--- BACKUPS ---*/

void error(char *);
void iniciar_servidor(char *);
void responder_a_clientes(int, key_t);


////////////////////// MANEJADOR de SEÑALES  ///////////////////////////
void sig_handler(int signal) {
  if (getppid() == pid_padre){
    if (signal == SIGUSR1) {
       // soy el hijo, proceso backupeador
      hacer_backup = 1;
      }
  }
}
////////////////////// FIN MANEJADOR de SEÑALES  ///////////////////////


int main(int argc, char* argv[]) {

	struct sockaddr_in clientaddr;
	socklen_t addrlen;
	//Default Values PATH = ~/ and PORT=10000
	char PORT[6];
	strcpy(PORT,"2017");

	int espacio_conexion=0; //

  if (signal(SIGUSR1, sig_handler) == SIG_ERR)
      printf("\nNo se pudo capturar SIGUSR1\n");

	/*--- MEMORIA COMPARTIDA ---*/
	clave_A = ftok ("/bin/ls", FTOK_A);
	clave_B = ftok ("/bin/ls", FTOK_B);
	clave_aux = ftok ("/bin/ls", FTOK_AUX);

	if (clave_A == -1 || clave_B == -1 || clave_aux == -1) {
	  printf("No consigo clave_A para memoria compartida\nEsta corriendo el servidor UDP?\n");
	  exit(0);
	}
	id_memoria_A = shmget (clave_A, sizeof(struct datos)*64, 0666);
	id_memoria_B = shmget (clave_B, sizeof(struct datos)*64, 0666);
	id_memoria_aux = shmget (clave_aux, sizeof(struct config)*1, 0666);

	if (id_memoria_A == -1 || id_memoria_B == -1 || id_memoria_aux == -1)  {
	  printf("No consigo Id para memoria compartida\n");
	  exit (0);
	}
	memoria_A = (struct datos *)shmat (id_memoria_A, NULL, 0);
	memoria_B = (struct datos *)shmat (id_memoria_B, NULL, 0);
	memoria_aux = (struct config *)shmat (id_memoria_aux, NULL, 0);


	if (memoria_A == NULL || memoria_B == NULL || memoria_aux == NULL) {
	  printf("No consigo memoria compartida\n");
	  exit (0);
	}

	/*--- /MEMORIA COMPARTIDA ---*/

  /*--- CREAR E INICIALIZAR SEMAFOROS ---*/

  clave_sem = ftok ("/bin/ls", FTOK_SEM);
            if (clave_sem == -1)
            {
              printf("No consigo clave para memoria compartida\n");
              exit(0);
            }

  id_sem = semget (clave_sem, NUM_SEM, 0666 );

            if (id_sem == (key_t)-1)
            {
              printf("No puedo conseguir clave para los semaforos\n");
              exit(0);
            }

  for (size_t i = 0; i < NUM_SEM; i++) {
  semctl(id_sem,i,SETVAL,1);  //Inicializa los semaforos (el 1 es que los pone en verde)
  }

  /*--- FIN CREAR E INICIALIZAR SEMAFOROS ---*/

  /* PROCESO BACKUP */
  pid_padre = getpid();


//pid_backupeador = fork();
  if (  fork() == 0 ) { // soy el hijo
    hacer_backup = 0;
    // LLAMAR AL MANEJADOR DE SEÑALES SI CORRESPONDE
  while(1){

    pause();

    if(hacer_backup == 1){

    ENTRAR(Operacion,sem_backup_in_progress);
    semop(id_sem,&Operacion,1);     //Verifica si no se esta haciendo backup
    puts("Escribiendo en disco...\n");

      if(memoria_aux->lectura == id_memoria_A)
        memoria_lectura = memoria_A;
      else
        memoria_lectura = memoria_B;

        fd_backup = fopen( "registros" , "a" );
        for (int i = 0; i < 65;i++){

          if ( (memoria_lectura + i)->tiempo != 0){

          fprintf(fd_backup, "%u\t%u\t%d\t%u\n",\
          (memoria_lectura + i)->tiempo, \
          (memoria_lectura + i)->lectura_sensor.ID,\
          (memoria_lectura + i)->lectura_sensor.temperatura,\
          (memoria_lectura + i)->lectura_sensor.RH );
          }
        }
        fclose(fd_backup);
        SALIR(Operacion,sem_backup_in_progress); // Permite volver a backup
        semop(id_sem,&Operacion,1);
        puts("Archivo de registros actualizado.\n");
        hacer_backup = 0;

      }
   }
  }else{ // SOY EL PADRE , server WEB -> continuo



  /* PROCESO BACKUP */

	puts("Servidor WEB iniciado"); //  en el puerto nro. %s%s%s en %s%s%s\n","\033[92m",PORT,"\033[0m","\033[92m",ROOT,"\033[0m");

	int i;
	for (i=0; i<CONEXIONES_MAX; i++)
		clientes[i]=-1; // todos en -1: significa que ningún cliente está conectados
	iniciar_servidor(PORT);

	// ACEPTAR conexiones
	while (1)	{
		addrlen = sizeof(clientaddr);
    // asocio descriptor de archivos devuelto por accept para identificar al cliente
		clientes[espacio_conexion] = accept (listenfd, (struct sockaddr *) &clientaddr, &addrlen);



		if (clientes[espacio_conexion]<0)
			error ("accept() error");
		else{

			if ( fork()==0 ) { // si soy el padre
				responder_a_clientes(espacio_conexion,memoria_aux->lectura);
				exit(0);
			}
		}

		while (clientes[espacio_conexion]!=-1)
       espacio_conexion = (espacio_conexion+1)%CONEXIONES_MAX;
	}
	return 0;
 }
}

// iniciar servidor
void iniciar_servidor(char *puerto)
{
	struct addrinfo hints, *res, *p;
  // hints: tipo de socket o protocolo preferido
  // hints=NULL -> cualquiera aceptable,
	// hints!=NULL -> apunto a la estructura

/*-- Preparo dirección en la que debe escuchar ---*/

  memset(&hints, 0, sizeof(hints)); // lleno hints con ceros
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM; // socket de flujo -> conexión TCP
	hints.ai_flags = AI_PASSIVE;
/*getaddrinfo()
﻿getaddrinfo() ﻿convierte cadenas de texto legible que representan ﻿nombres de host ﻿o
﻿Direcciones IP ﻿en un ﻿asigna de forma dinámica ﻿lista enlazada ﻿de ﻿struct addrinfo
﻿estructuras.*/

// Las conexiones que quieran ser atendidas deberán entrar por el puerto 2017
	if (getaddrinfo( NULL, puerto, &hints, &res) != 0)	{
		perror ("getaddrinfo() error");
		exit(1);
	}
	// creo sockets y les asocio las direcciones
	for ( p = res; p!=NULL; p=p->ai_next ) {

		listenfd = socket (p->ai_family, p->ai_socktype, 0);

		if (listenfd == -1)
      continue;
		if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
      break;
	}
	if ( p == NULL ) {
		perror ("Error en socket() o bind()");
		exit(1);
	}

	freeaddrinfo(res); // Libero la memoria que usé para res

	// listen for incoming connections
	if ( listen (listenfd, 1000000) != 0 )
	{
		perror("listen() error");
		exit(1);
	}
}

//client connection

void responder_a_clientes(int n, key_t id_mem_lectura) {

	char mesg[MAX_MSG], *reqline[4], *id_sensor, *intervalo_sensor, *obtener_campo[4], path[MAX_MSG]; //, data_to_send[BYTES],
	char filas[LONG_MAX_LINEA*NRO_SENSORES];
	char fila[LONG_MAX_LINEA];
	char html[MAX_HTML];
	int rcvd; //, bytes leidos //, fd

	char linea[LONG_MAX_LINEA];	 // max col registros
	time_t t;
	const char *format = "%d/%m/%Y %H:%M:%S hs";
	struct tm lt;
	char res[32]; // marca_tempral


	int ids_sensores[NRO_SENSORES] = {0};
	int arreglo_intervalos[4] = {1,6,12,24};
	int size = NRO_SENSORES;
	//int presente;
	int temp;
	int vueltas1h,vueltas6h,vueltas12h,vueltas24h;
	int hora_entero_max; //,hora_entero_max_temporal;
	signed int promedio_temperatura1,promedio_temperatura6,promedio_temperatura12,promedio_temperatura24;
	int promedio_humedad1,promedio_humedad6,promedio_humedad12,promedio_humedad24;
	signed int max_t,min_t,ultima_t;
	unsigned int max_h,min_h,ultima_h;

  int diferencia_segundos;
  int promedio_temperatura[257] = {0};
  int promedio_humedad[257] = {0};
  int vueltas[257] = {0};

	if(id_mem_lectura == id_memoria_A){
		memoria_lectura = memoria_A; /* puts("ahora es mem A");  printf("A:%d\n",memoria_lectura->lectura_sensor.ID);*/
  }else{
		memoria_lectura = memoria_B; /*		puts("ahora es mem B"); */
	}

	memset( (void*)mesg, (int)'\0', MAX_MSG );

	rcvd=recv(clientes[n], mesg, MAX_MSG, 0);

	if (rcvd<0)    // receive error
		fprintf(stderr,("Error en recv()\n"));
	else if (rcvd==0)    // receive socket closed
		fprintf(stderr,"El cliente se ha desconectado.\n");
	else    // message received
	{
		printf("%s", mesg);
		reqline[0] = strtok (mesg, " \t\n"); // obtener la 1ra linea del mensaje ->  GET / .....

		if ( strncmp(reqline[0], "GET\0", 4) == 0 ) // si 1ra linea arranca con GET\0
		{
			reqline[1] = strtok (NULL, " \t");
			reqline[2] = strtok (NULL, " \t\n");
			// comparo los últimos 8 caracteres de la 1ra linea
			if ( strncmp( reqline[2], "HTTP/1.0", 8)!=0 && strncmp( reqline[2], "HTTP/1.1", 8)!=0 ) {
			  write(clientes[n], "HTTP/1.0 400 Bad Request\n", 25); // <- si no es HTTP/1.1, ni HTTP/1.0
			}else{
        // Inicializo en vacío variables que se irán concatenando
			  strcpy(filas, "");
			  strcpy(html, "");

      /*--- Armo cabecera del documento HTML ---*/

			fd_temporal = fopen( "web/header.inc" , "r" );
			while (fgets(linea, 70, fd_temporal) != NULL)	{
		    strcat(html, linea);
			}
			fclose(fd_temporal); // cierro fd para reutilizar variable

/*
=================================
  ÚLTIIMOS REGISTROS EN MEMORIA
=================================
*/
/*--- Si no pedí ningún recurso en particular, o pedi "lecturas" ---*/

			if ( strncmp(reqline[1], "/\0", 2)==0 || strncmp(reqline[1], "/lecturas\0", 10) == 0){
						strcat(html, " \n\
						[<a href='sensor'>Lecturas por sensor</a>]  \n\
						[<a href='promedios'>Valores medios</a>]<br/>\n\
						<table class='responstable'><tr><td colspan='4' class='enmem'>Últimos registros en memoria</td></tr>\
						<tr><!--<th>Timestamp</th>--><th>ID de Sensor</th><th>Temperatura[&#176;C]</th>\
						<th>Humedad Relativa [&#x25;]</th></tr>");

			  		strcpy(filas, "");
            VERIFICAR(Operacion,sem_swap_ready);
            while (Operacion.sem_op == 0) { //semaforo en verde ->sigo y muestro

            ENTRAR(Operacion,sem_mem_be_reading);
            Operacion.sem_flg =  IPC_NOWAIT;  // permite que varios clientes lean simultaneamente
            semop(id_sem,&Operacion,1);     ////Espera que dejen de leer

						for (int i = 0; i < 65;i++){

              if ( (memoria_lectura + i)->tiempo != 0 ){ //si hay algo en ese registro
/*             SI QUIERO EN LA 1ra COLUMNA DE LA TABLA VER LA MARCA DE TIEMPO
  							t = (time_t)(memoria_lectura + i)->tiempo;
  							localtime_r(&t, &lt); // como localtime pero almacenando en estructura definida por mi
  							strftime(res, sizeof(res), format, &lt);
*/
  							sprintf(fila, "<tr><!--<td>%s</td>--><td>%u</td><td>%.2f</td><td>%u</td></tr>", \
  							res, \
  							(memoria_lectura + i)->lectura_sensor.ID, \
  							(float)((memoria_lectura + i)->lectura_sensor.temperatura)/10, \
  							(memoria_lectura + i)->lectura_sensor.RH );
  							strcat(filas, fila);
              }
						}

            SALIR(Operacion,sem_mem_be_reading);
            Operacion.sem_flg =  0;  // reinicializa el flag revirtiendo el IPC_NOWAIT
            semop(id_sem,&Operacion,1);
						strcat(html, filas);
          }
						fclose(fd_temporal); // cierro fd para reutilizar variable
						strcat(html, filas);
						strcat(html,"</table>\n");
					}else if ( strncmp(reqline[1], "/sensor", 7) == 0 ){

/*
=================================
     VER SENSOR ESPECIFICO
=================================
*/

						//ej.: reqline /sensor?1&3
						id_sensor = strtok (reqline[1], "?");
						id_sensor = strtok (NULL, "?"); // id sensor

						strcat(html, "\
						[<a href='/'>Lecturas recientes</a>]  \n\
						[<a href='promedios'>Valores medios</a>]\n<br/>");

            strcat(html,"<script>\
						$(function() { \
						 $( '#sensor' ).change(function() { \
						    window.location.replace('http://127.0.0.1:2017/sensor?'+$( this ).val()); \
						  }); \
						});	\
						</script>\
						<br/>");


            strcat(html,"<select id='sensor'><option>-- Seleccione un sensor --</option>");

            hacer_backup = 0;

						fd_temporal = fopen( "registros" , "r" );
						temp = 0;
						while (fgets(linea, LONG_MAX_LINEA, fd_temporal) != NULL)	{

						 if (strlen(linea)>1){
						    obtener_campo[0] = strtok(linea, " \t"); // primer campo
						    obtener_campo[1] = strtok(NULL, " \t"); // 2do campo -> id sensor
						    ids_sensores[temp] = atoi(obtener_campo[1]);
							temp++;
					      }
						}
					   // llevar a valores unicos
					   for (int i = 0; i < size; i++) {
						  for (int j = i + 1; j < size;) {
							 if (ids_sensores[j] == ids_sensores[i]) {
								for (int k = j; k < size; k++) {
								   ids_sensores[k] = ids_sensores[k + 1];
								}
								size--;
							 } else
								j++;
						  }
					   }
						for (int i = 0; i < size-1; i++) {
						  strcpy(fila, ""); // remover selected si está ajustado

							if ( id_sensor != NULL && atoi(id_sensor) == ids_sensores[i] ){
						    sprintf(fila,"selected='selected'"); // dejar seleccionado option si corresponde
                }
						  sprintf(filas, "<option value='%d' %s>Sensor %d</option>", ids_sensores[i], fila, ids_sensores[i] );
 						  strcat(html,filas);

						}
						strcpy(filas, "");

						strcat(html,"</select>");

						if (id_sensor != NULL && id_sensor != 0){

/* DATOS DE SENSOR SELECCIONADO */

						fd_temporal = fopen( "registros" , "r" );

						promedio_temperatura1 = 0;
						promedio_humedad1 = 0;
						promedio_temperatura6 = 0;
						promedio_humedad6 = 0;
						promedio_temperatura12 = 0;
						promedio_humedad12 = 0;
						promedio_temperatura24 = 0;
						promedio_humedad24 = 0;
						vueltas1h = vueltas6h = vueltas12h = vueltas24h = 0;

						max_h = min_h = max_t = min_t = 0;
						temp = 0;
						while (fgets(linea, LONG_MAX_LINEA, fd_temporal) != NULL)	{

					     if (strlen(linea) > 1){

						   obtener_campo[0] = strtok(linea, " \t"); // primer campo
						   obtener_campo[1] = strtok(NULL, " \t"); // 2do campo -> id sensor
						   /* SI EL SENSOR DE LA LINEA ACTUAL = SENSOR SELECCIONADO */

						   if (strcmp(obtener_campo[1],id_sensor) == 0){
 							     // si primera vuelta:
						       if (temp == 0)  // inicializar marca de tiempo con lo del 1er registro
							       hora_entero_max = atoi(obtener_campo[0]);

                 // buscar marca temporal mas reciente en disco y

							   if ( hora_entero_max < atoi(obtener_campo[0]) ){   // ULTIMA LECTURA
							 	    hora_entero_max = atoi(obtener_campo[0]);
							   }
						    temp++;
						   }
						  }
						 }

						// obtener marca temporal mas reciente formateada
						t = (time_t)hora_entero_max;
						localtime_r(&t, &lt);
						strftime(res, sizeof(res), format, &lt);

						temp = 0; // reutilizar variable de vuelta

						sprintf(fila,"<table class='responstable'><tr><th>Magnitud</th><th>Última lectura<br/>%s</th><th>Máximo</th><th>Mínimo</th></tr>\n",res);
						strcat(html,fila);

						fseek(fd_temporal, 0, SEEK_SET); // VOLVER al principio del archivo para leer los demas campos
						while (fgets(linea, LONG_MAX_LINEA, fd_temporal) != NULL)	{

							if (strlen(linea)>1){
							obtener_campo[0] = strtok(linea, " \t"); // fecha unix
							obtener_campo[1] = strtok(NULL, " \t"); // id sensor
							obtener_campo[2] = strtok(NULL, " \t"); // temp
							obtener_campo[3] = strtok(NULL, " \t"); // hum


							/* SI EL SENSOR DE LA LINEA ACTUAL = SENSOR SELECCIONADO */
							// al final temp++ solo para este sensor
							if (strcmp(obtener_campo[1],id_sensor) == 0){

							  // si primera vuelta:
							  if (temp == 0){

								 // ajustar maximos y minimos con el 1er valor leido
								 ultima_h = max_h = min_h = atoi(obtener_campo[3]);
								 ultima_t = max_t = min_t = atoi(obtener_campo[2]);

							  }
							  // ajustar maximos y minimos en cada pasada
							  if ( max_h < atoi(obtener_campo[3]) )
								  max_h = atoi(obtener_campo[3]);

							  if ( min_h > atoi(obtener_campo[3]) )
								  min_h = atoi(obtener_campo[3]);

							  if ( max_t < atoi(obtener_campo[2]) )
								  max_t = atoi(obtener_campo[2]);

							  if ( min_t > atoi(obtener_campo[2]) )
								  min_t = atoi(obtener_campo[2]);

							  // sumar para luego obtener promedios promedios segun el intervalo seleccionado

							  // Lecturas dentro de la ultima hora registrada
							  if ( (hora_entero_max - atoi(obtener_campo[0])) <= 3600 ){
							     vueltas1h++;
							     promedio_humedad1 = promedio_humedad1 + atoi(obtener_campo[3]);
							     promedio_temperatura1 = promedio_temperatura1 + atoi(obtener_campo[2]);
							   }
							  // Lecturas dentro de las ultimas 6 horas registrada
							  if ( (hora_entero_max - atoi(obtener_campo[0])) <= 21600 ){
							     vueltas6h++;
							     promedio_humedad6 = promedio_humedad6 + atoi(obtener_campo[3]);
							     promedio_temperatura6 = promedio_temperatura6 + atoi(obtener_campo[2]);
							   }
							  // Lecturas dentro de las ultimas 12 horas registrada
							  if ( (hora_entero_max - atoi(obtener_campo[0])) <= 43200 ){
							     vueltas12h++;
							     promedio_humedad12 = promedio_humedad12 + atoi(obtener_campo[3]);
							     promedio_temperatura12 = promedio_temperatura12 + atoi(obtener_campo[2]);
							   }
							  // Lecturas dentro de las ultimas 24 horas registrada
							  if ( (hora_entero_max - atoi(obtener_campo[0])) <= 86400 ){
							     vueltas24h++;
							     promedio_humedad24 = promedio_humedad24 + atoi(obtener_campo[3]);
							     promedio_temperatura24 = promedio_temperatura24 + atoi(obtener_campo[2]);
							   }

							  if ( hora_entero_max == atoi(obtener_campo[0]) ) { // ULTIMA LECTURA
								ultima_t = atoi(obtener_campo[2]);
								ultima_h = atoi(obtener_campo[3]);

							  }

							  temp++; // sumo cantidad de registros para este sensor

							  }
							}

						}
						fclose(fd_temporal); // cierro fd para reutilizar variable

						sprintf(fila, "<tr><td>Temperatura</td><td>%.1f&#176;C</td><td>%.1f&#176;C</td><td>%.1f&#176;C</td></tr>", (float)ultima_t/10, (float)max_t/10, (float)min_t/10 );
						strcat(html, fila);
						sprintf(fila, "<tr><td>Humedad</td><td> %u&#x25;</td><td> %u&#x25; </td><td> %u&#x25; </td></tr>", ultima_h, max_h, min_h );
						strcat(html, fila);

						strcpy(fila,"<table class='responstable'><tr><th>Magnitud</th><th>1hs</th><th>6hs</th><th>12hs</th><th>24hs</th></tr>\n");
						strcat(html,fila);


						sprintf(fila, "<tr><td>Temperatura promedio</td><td>%.1f&#176;C</td><td>%.1f&#176;C</td><td>%.1f&#176;C</td><td>%.1f&#176;C</td></tr>", (float)promedio_temperatura1/(10*vueltas1h), (float)promedio_temperatura6/(10*vueltas6h), (float)promedio_temperatura12/(10*vueltas12h), (float)promedio_temperatura24/(10*vueltas24h));
						strcat(html, fila);
						sprintf(fila, "<tr><td>Humedad promedio</td><td> %.2f&#x25;</td><td> %.2f&#x25; </td><td> %.2f&#x25; </td><td> %.2f&#x25; </td></tr>", (float)promedio_humedad1/vueltas1h,(float)promedio_humedad6/vueltas6h, (float)promedio_humedad12/vueltas12h, (float)promedio_humedad24/vueltas24h );
						strcat(html, fila);
						}

/* FIN DATOS SENSOR */


/*
=================================
  LECTURAS PROMEDIO DE SENSORES
=================================
*/

}else if ( strncmp(reqline[1], "/promedios", 10)==0 ){
						//strcat(html, filas);
						//printf("%s\n", );

						//strcat(html,"Estoy pidiendo promedios en un intervalo\n");
						//reqline /sensor?1&3
						intervalo_sensor = strtok (reqline[1], "?");
						intervalo_sensor = strtok (NULL, "?"); // id intervalo

						strcat(html, "\
						[<a href='/'>Lecturas recientes</a>]  \n\
						[<a href='sensor'>Lecturas por sensor]</a>\n<br/>");

						// armar destino: sensor?id_sensor&intervalo
						strcat(html,"<script>\
						$(function() { \
						 $( '#intervalo' ).change(function() { \
						    window.location.replace('http://127.0.0.1:2017/promedios?'+ $( this ).val()); \
						  }); \
						});	\
						</script>\
						<br/>");


						fd_temporal = fopen( "registros" , "r" );
						temp = 0;
						while (fgets(linea, LONG_MAX_LINEA, fd_temporal) != NULL)	{

						 if (strlen(linea)>1){
						    obtener_campo[0] = strtok(linea, " \t"); // primer campo
						    obtener_campo[1] = strtok(NULL, " \t"); // 2do campo -> id sensor
						    ids_sensores[temp] = atoi(obtener_campo[1]);
							temp++;
					      }
						}
					   // llevar a valores unicos para obtener una lista de sensores sin repeticion
					   for (int i = 0; i < size; i++) {
						  for (int j = i + 1; j < size;) {
							 if (ids_sensores[j] == ids_sensores[i]) {
								for (int k = j; k < size; k++) {
								   ids_sensores[k] = ids_sensores[k + 1];
								}
								size--;
							 } else
								j++;
						  }
					   }

						strcat(html,"<select id='intervalo'>\
						<option>--Seleccione un intervalo--</option>");

						for (int i = 0; i < 4; i++){
						  strcpy(fila, "");
							if ( intervalo_sensor != NULL && atoi(intervalo_sensor) == i+1 )
								sprintf(fila,"selected='selected'"); // dejar seleccionado option si corresponde
								sprintf(filas, "<option value='%d' %s>Ultima/s %dhs</option>", (i+1), fila, arreglo_intervalos[i] );
								strcat(html,filas);
						}
						strcat(html,"</select>");

						if (intervalo_sensor != NULL && intervalo_sensor != 0){

  						fd_temporal = fopen( "registros" , "r" );

  						max_h = min_h = max_t = min_t = 0;
  						temp = 0;

              /*--- definir intervalos para calcular promedios ---*/
              switch (atoi(intervalo_sensor)) {
                case 1:
                  diferencia_segundos = 3600; // ultima hora
                case 2:
                  diferencia_segundos = 21600; // ultimas 6hs
                case 3:
                  diferencia_segundos = 43200;// ultimas 12hs
                case 4:
                  diferencia_segundos = 86400; // ultimas 24hs
              default:
                  diferencia_segundos = 3600;
              }

						while (fgets(linea, LONG_MAX_LINEA, fd_temporal) != NULL)	{

					     if (strlen(linea) > 1){
						       obtener_campo[0] = strtok(linea, " \t"); // primer campo
						       obtener_campo[1] = strtok(NULL, " \t"); // 2do campo -> id sensor
							   // si primera vuelta:
						       if (temp == 0){
							     // inicializar marca de tiempo con lo del 1er registro
							     hora_entero_max = atoi(obtener_campo[0]);
							     //printf("1 !!! marca tiempo max: %d\n",atoi(obtener_campo[0]));
							     }

							   // buscar marca temporal mas reciente

							   if ( hora_entero_max < atoi(obtener_campo[0]) ){   // ULTIMA LECTURA
							    	hora_entero_max = atoi(obtener_campo[0]);
							 	//printf("marca tiempo max: %d\n",hora_entero_max);
							   }
						    temp++;

						  }
						 }

						// obtener marca temporal mas reciente formateada
						t = (time_t)hora_entero_max;
						localtime_r(&t, &lt);
						strftime(res, sizeof(res), format, &lt);

						temp = 0; // reutilizar variable de vuelta

            // Ubicarme de nuevo al inicio del archivo de registros en disco
						fseek(fd_temporal, 0, SEEK_SET);

            /*--- LIMPIAR VARIABLES TEMPORALES DE SUMAS PARA CALCULAR LUEGO PROMEDIOS---*/
            memset(&promedio_humedad, 0, sizeof(promedio_humedad));
            memset(&promedio_temperatura, 0, sizeof(promedio_temperatura));

						while (fgets(linea, LONG_MAX_LINEA, fd_temporal) != NULL)	{

							if (strlen(linea)>1){
							obtener_campo[0] = strtok(linea, " \t"); // fecha unix
							obtener_campo[1] = strtok(NULL, " \t"); // id sensor
							obtener_campo[2] = strtok(NULL, " \t"); // temp
							obtener_campo[3] = strtok(NULL, " \t"); // hum

                  // contribuir al promedio segun intervalo seleccionado
  							if ( (hora_entero_max - atoi(obtener_campo[0])) <= diferencia_segundos){

  							  // sumar para luego obtener promedios promedios segun el intervalo seleccionado
//promedio_humedad1 = promedio_humedad1 + atoi(obtener_campo[3]);
						      promedio_humedad[atoi(obtener_campo[1])] = promedio_humedad[atoi(obtener_campo[1])] + atoi(obtener_campo[3]);
						      promedio_temperatura[atoi(obtener_campo[1])] = promedio_temperatura[atoi(obtener_campo[1])] + atoi(obtener_campo[2]);
                  vueltas[atoi(obtener_campo[1])]++;
                  temp++; // sumo cantidad de registros para este sensor

							  }
							}

						}
						fclose(fd_temporal); // cierro fd para reutilizar variable

            strcat(html,"<table class='responstable'>\n\
                         <tr><th>Sensor</th><th>Temperatura promedio</th><th>Humedad promedio</th></tr>\n");

             for (int i = 0; i < size-1; i++) {


               if (vueltas[ids_sensores[i]] != 0 ){
                 sprintf(fila, "<tr><td>%d</td><td>%.2f&#176;C</td><td>%.1f&#x25;</td></tr>\n",\
                      ids_sensores[i],\
                      ((float)promedio_temperatura[ids_sensores[i]])/(10*vueltas[ids_sensores[i]]), \
                      ((float)promedio_humedad[ids_sensores[i]])/vueltas[ids_sensores[i]]);
  						  strcat(html, fila);
               }
             }
            strcat(html, "</table>");
						}


}/* FIN VALORES PROMEDIOS */

        /*--- Escribir el pie de la página HTML ---*/
				fd_temporal = fopen( "web/footer.inc" , "r" );
				while (fgets(linea, 70, fd_temporal) != NULL)	{
				strcat(html, linea);
				}
				fclose(fd_temporal);
				strcpy(path,html);

        // Envío un mensaje al socket, aviso al cliente "n" que pidió
        // el recurso que voy a enviar una respuesta por protocolo HTTP
			  send(clientes[n], "HTTP/1.0 200 OK\n\n", 17, 0);
        // Envío la página entera
				write (clientes[n], path, (strlen(path) + 1 ) * sizeof(char));

			}
		}

	}

   //Cerrar el socket para este cliente
	shutdown (clientes[n], SHUT_RDWR);
  // SHUT_RDWR es poner 2 => se desabilitan la recepción y el envío, igual que en close ().
	close(clientes[n]);

	clientes[n]=-1; // -1 para avisar que este cliente ya no está conectado
	close(listenfd);
}
