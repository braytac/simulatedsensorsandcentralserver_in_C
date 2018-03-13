#include<stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>   // Needed for internet address structure.
#include <sys/socket.h>
#include <fcntl.h> // for open
#include <unistd.h> // for close
#include <sys/types.h>// -> inet_pton
#include <arpa/inet.h> // -> inet_pton

#define LEN  6

int main(int argc, char* argv[]){

char out_buf[LEN];
char* ptr_out;
int PORT_NUM=2019,addr_len,leng;
unsigned int fd_udp;
struct sockaddr_in servidor_udp;

#pragma pack(1)  // exact fit - no padding, 1, 2, 4, 8...
struct datos {
unsigned char ID; /* Identificación de sensor 0 – 255 */
int temperatura; /* Valor de temperatura multiplicado por 10 */
unsigned char RH; /* Valor de Humedad Relativa Ambiente */
};
#pragma pack() // deja alineación como se le canta a gcc

struct datos data;

fd_udp = socket(AF_INET, SOCK_DGRAM, 0);

servidor_udp.sin_family = AF_INET;
servidor_udp.sin_port = htons(PORT_NUM);
inet_pton(AF_INET, "127.0.0.1",&servidor_udp.sin_addr.s_addr);

// No hace falta bind
time_t random;
srand((unsigned)time(&random));  // da implicit declaration pero todo bien igual.
int j=1;
for (size_t i = 0; i < 368; i++) {
if (j == 64)
   j = 0;
data.ID = (rand() % 255)+1; /* Identificación de sensor 1 – 255 */

/* pasarme a veces por debajo y a veces por encima */

if ( (j%2) == 0)
  data.temperatura = -505 + rand() % 33; /* Valor de temperatura multiplicado por 10 */
else
  data.temperatura = 1100 + rand() % 310; /* Valor de temperatura multiplicado por 10 */

data.RH = 80 + rand() % 26 ; /* Valor de Humedad Relativa Ambiente */


printf("%d\tID=%u\tTEMP=%d\tRH=%u\n", j, (unsigned int)data.ID, data.temperatura, (unsigned int)data.RH);
i++;
j++;
data.temperatura = htonl(data.temperatura);
ptr_out = (char*)&data;

sendto(fd_udp, ptr_out, sizeof(out_buf), 0,  (struct sockaddr *)&servidor_udp, sizeof(servidor_udp));
//usleep(500000);
sleep(1);
}



close(fd_udp);

}
