#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>   // Needed for internet address structure.
#include <sys/socket.h>
#include <fcntl.h> // for open
#include <unistd.h> // for close
#include <time.h>
#include <sys/shm.h> // shm
#include <sys/ipc.h>
#include <sys/sem.h>
/*   DEFINICIONES PARA SEMAFOROS   */
#define NUM_SEM                       4     // Numero de semaforos
#define sem_write                     0
#define sem_swap_ready                1
#define sem_mem_be_reading            2
#define sem_backup_in_progress        3
#define ENTRAR(OP,n) ({(OP).sem_op = -1;(OP).sem_num = n;})//Define entrar al semaforo n
#define SALIR(OP,n)  ({(OP).sem_op = +1;(OP).sem_num = n;})//Define salir al semaforo n
/* FIN  DEFINIONES PARA SEMAFOROS    */

/*  DEFINICIONES GENERALES    */
#define LEN  6        // bytes info sensores
#define BUFFER_LEN 64 // tamaño en datos del buffer ping-pong
#define TEMP_MIN  -550 // Valor minimo de temperatura correctos
#define TEMP_MAX  1399 // Valor maximo de temperatura correctos
#define RH_MAX    100  // Valor maximo de RH correcto

#define FTOK_A    1
#define FTOK_B    769
#define FTOK_AUX  2531
#define FTOK_SEM  7945

/*    INICIO PROGRAMA     */
int main(int argc, char* argv[]){

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
  key_t escritura;     /* ID de la memoria compartida para escritura */
  key_t lectura;       /* ID de la memoria compartida para lectura */
  int contador;        /* Indicador de por donde se esta escribiendo
                          la memoria con ID escritura */
};

  struct datos data;

/*--- Memoria compartida ---*/
  key_t clave_A, clave_B, clave_aux, aux;
	int id_memoria_A, id_memoria_B, id_memoria_aux;
	struct datos* memoria_A = NULL;
	struct datos* memoria_B = NULL;
  struct config* memoria_aux = NULL;

/*--- /Memoria compartida ---*/

/*--- Marca temporal y log errores ---*/
FILE *entrada;
char marca_temporal[80];
time_t rawtime;
struct tm *dt;
/*--- /Marca temporal ---*/

char recibidos[LEN];
unsigned int PORT_NUM=2019,addr_len,leng;
unsigned int fd_udp;
struct sockaddr_in servidor_udp;
struct sockaddr_in cliente_udp;


/*--- MEMORIA COMPARTIDA ---*/
clave_A = ftok ("/bin/ls", FTOK_A);
clave_B = ftok ("/bin/ls", FTOK_B);
clave_aux = ftok ("/bin/ls", FTOK_AUX);

if (clave_A == -1 || clave_B == -1 || clave_aux == -1) {
  printf("No consigo clave_A para memoria compartida\n");
  exit(0);
}
id_memoria_A = shmget (clave_A, sizeof(struct datos)*BUFFER_LEN, 0666 | IPC_CREAT);
id_memoria_B = shmget (clave_B, sizeof(struct datos)*BUFFER_LEN, 0666 | IPC_CREAT);
id_memoria_aux = shmget (clave_aux, sizeof(struct config)*1, 0666 | IPC_CREAT);


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

memoria_aux->escritura = id_memoria_A; // memoria escritura = mem A al iniciar
memoria_aux->lectura = id_memoria_B;   // memoria  lectura = mem B al iniciar
memoria_aux->contador = 0;             // Contador en cero al inicio

/*--- FIN MEMORIA COMPARTIDA ---*/

/*--- CREAR E INICIALIZAR SEMAFOROS ---*/
key_t clave_sem;
int id_sem;
struct sembuf Operacion;

clave_sem = ftok ("/bin/ls", FTOK_SEM);
          if (clave_sem == -1)
          {
            printf("No consigo clave para memoria compartida | semaf\n");
            exit(0);
          }

id_sem = semget (clave_sem, NUM_SEM, 0666 | IPC_CREAT); // retorna el identificador de semáforo asociado con la clave.

          if (id_sem == (key_t)-1)
          {
            printf("No puedo conseguir clave para los semaforos\n");
            exit(0);
          }
for (size_t i = 0; i < NUM_SEM; i++) {
semctl(id_sem,i,SETVAL,1);  //Inicializa los semaforos (el 1 es que los pone en verde)
}

/*--- FIN CREAR E INICIALIZAR SEMAFOROS ---*/


if (  fork() == 0 ) { // soy el hijo
// Ejecutar desde acá el proceso PS0 para que sea servidor_UDP quien
// estableció la memoria compartida y semáforos primero
if ( execlp("./PS0", "./PS0", NULL) == -1)
  puts("El proceso del sensor PS0 se ejecutó correctamente\n");
else
  puts("Ocurrió un erro al intentar ejecutar el proceso del sensor PS0\n");
}

fd_udp = socket(AF_INET, SOCK_DGRAM, 0);

servidor_udp.sin_family = AF_INET;
servidor_udp.sin_port = htons(PORT_NUM);
servidor_udp.sin_addr.s_addr = htonl(INADDR_ANY);

bind(fd_udp,(struct sockaddr *)&servidor_udp, sizeof(servidor_udp));
/* abro archivo log y creo la memoria compartida*/

cliente_udp.sin_family = AF_INET;
cliente_udp.sin_port = htons(PORT_NUM);
cliente_udp.sin_addr.s_addr = htonl(INADDR_ANY);
addr_len = sizeof(cliente_udp);
puts("Escuchando...");
  while (0 < (leng = recvfrom(fd_udp, recibidos, LEN, 0, \
   (struct sockaddr *)&cliente_udp, &addr_len))) {

/*--- SI BUFFER DE ENTRADA SE LLENÓ ---*/
   if (memoria_aux->contador > BUFFER_LEN - 1){
       ENTRAR(Operacion,sem_write);    //Bloquea la escritura en la memoria compartida
       semop(id_sem,&Operacion,1);
       ENTRAR(Operacion,sem_backup_in_progress);
       semop(id_sem,&Operacion,1);     //Verifica si no se esta haciendo backup
       ENTRAR(Operacion,sem_swap_ready);
       semop(id_sem,&Operacion,1);     //Cambia el semaforo para swapear
       ENTRAR(Operacion,sem_mem_be_reading);
       semop(id_sem,&Operacion,1);     ////Espera que dejen de leer

       aux = memoria_aux->escritura;
       memoria_aux->escritura = memoria_aux->lectura;
       memoria_aux->lectura = aux;
       memoria_aux[0].contador=0;

       SALIR(Operacion,sem_write);
       semop(id_sem,&Operacion,1);
       SALIR(Operacion,sem_backup_in_progress); // Permite volver a backup
       semop(id_sem,&Operacion,1);
       SALIR(Operacion,sem_mem_be_reading);
       semop(id_sem,&Operacion,1);
       SALIR(Operacion,sem_swap_ready);
       semop(id_sem,&Operacion,1);        //Permite volver a leer

       puts("\n\nUDP server intercambiando buffer\n\n");

       //envío señal al servidor WEB para volcar lecturas a disco
       system("pkill -SIGUSR1 servidor_WEB");

   }
/*--- FIN de SI BUFFER DE ENTRADA SE LLENÓ ---*/

data.lectura_sensor = *(struct lectura *)recibidos; // castear los datos recibidos a la variable tipo datos
data.lectura_sensor.temperatura = ntohl(data.lectura_sensor.temperatura); // cambiar como vienen los datos por la red.

/*-- Tiempo en formato legible por humanos --*/

time( &rawtime );
data.tiempo = rawtime;
dt = localtime( &rawtime );
strftime(marca_temporal,80,"%x %X hs", dt);


/* Si el dato es correcto entra aca para guardarlo en la memoria compartida*/

if( (TEMP_MIN <= data.lectura_sensor.temperatura && data.lectura_sensor.temperatura <= TEMP_MAX) && \
  (data.lectura_sensor.RH <= RH_MAX) ) {

    printf("Sensor %u envía:   \tTEMP = %d  \tRH = %u   \ten %s\n",
    (unsigned int)data.lectura_sensor.ID,
    (signed int)data.lectura_sensor.temperatura,
    (unsigned int)data.lectura_sensor.RH,
    marca_temporal);


    ENTRAR(Operacion,sem_write);
    semop(id_sem,&Operacion,1);

    if(memoria_aux->escritura == id_memoria_A){
      *(memoria_A + memoria_aux->contador) = data;
      memoria_aux->contador++;
    }else{
      *(memoria_B + memoria_aux->contador) = data;
      memoria_aux->contador++;
    }
          SALIR(Operacion,sem_write);
          semop(id_sem,&Operacion,1);
}else{
  /* Si el dato es incorrecto entra aca para guardarlo en el log de errores*/
  entrada = fopen( "error.log" , "a" );
  // fprintf(entrada, "-------------------------- %s --------------------------------\n", marca_temporal); // Separador Posible
  fprintf(entrada, "%s\tID=%d\t\tTEMP=%d\tRH=%d\n",marca_temporal, data.lectura_sensor.ID, data.lectura_sensor.temperatura, data.lectura_sensor.RH  );
  fclose(entrada);
 }
}
close(fd_udp);
system("pkill -SIGTERM servidor_WEB");
system("pkill -SIGTERM PS0");
return 0;
}
