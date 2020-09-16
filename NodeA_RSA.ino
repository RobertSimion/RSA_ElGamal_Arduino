#include <mcp_can_dfs.h>
#include <SPI.h>
#include <mcp_can.h>

#include <math.h>
#include "BigNumber.h"
#include <stdlib.h>

#define  SPICSPIN  10
#define  SCKPIN  13
#define  SIPIN  11
#define  SOPIN  12
#define  ISRPIN 9

// GND_CAN - POWER GND MCU
// VCC_CAN - POWER 5V MCU

MCP_CAN CAN(SPICSPIN);

void setup(){
  
    Serial.begin(115200);
    while (CAN_OK != CAN.begin(CAN_500KBPS,MCP_8MHz)){
        Serial.println("CAN BUS init Failed");
        delay(100);
    }
    Serial.println("CAN BUS Shield Init OK!");
    BigNumber::begin();
}

enum RSA_STATE{
  STATE_UNINIT,
  STATE_INIT_PARAM,
  STATE_RECV_PK,
  STATE_ENCRYPT,
  STATE_TX,
  STATE_STOP
};

RSA_STATE g_state = STATE_UNINIT; 
int g_p, g_q, g_e, g_d, g_N, g_fi_n, g_pk_B_int;

unsigned char g_msg[8] = {'r','o','b','e','r','t','1','8'};

unsigned char *g_ack0;
unsigned char *g_ack1;
unsigned char *g_encrypted_buffer;

unsigned char  g_len_ack1;
unsigned char g_pk_B_size;
unsigned char g_pk_B[2];


unsigned char* encrypt(int N, int e){

  unsigned char *encrypted_buffer = (unsigned char*)malloc(8 * sizeof(unsigned char));
  
  BigNumber pk = BigNumber(e);
  BigNumber n = BigNumber(N);
  
  for(int i = 0; i < 8; i++){
    BigNumber msg = BigNumber((int)g_msg[i]);
    *(encrypted_buffer + i) = msg.powMod(pk,n);
  }
 
  return encrypted_buffer;
}

int gcd(int a, int b){
  
   while(a != b){
    
      if(a > b){
            a -= b;
      }
      else{
            b -= a;
      }
   }
   return a;
}

int generate_pk(int fi){


  
  int j = 0;
  int* e = (int *)malloc((fi - 1 - 2 + 1) * sizeof(int));
  
  memset(e, 0, (fi - 1 - 2 + 1)*sizeof(int));
  
  for(int i = 2; i <= fi - 1; i++){
     if(gcd(i, fi) == 1){
      *(e + j) = i;
      j++;
     }
  }
  
  int pk_index = rand() % j;
  
  return *(e + pk_index);
}

int generate_pv_k(int pk,int fi){
  
    int pvk;
    for(pvk = 0; pvk < fi - 1; pvk++){
      if((pvk * pk) % fi == 1)break; 
    }
    return pvk;
}

void loop(){
  
  static unsigned char esc = 0;
  
  if(esc == 1)exit(0);
  
  switch(g_state){
    
    case STATE_UNINIT:{
      
      g_state = STATE_INIT_PARAM;
      Serial.println("START state");
      
      break;
    }
    
    case STATE_INIT_PARAM:{
      
      Serial.println("INIT PARAM state");
      //19,13
      g_p = 19;
      Serial.println("p param: ");
      Serial.println(g_p);
      g_q = 13;
      Serial.println("q param: ");
      Serial.println(g_q);
      g_N = g_p * g_q;
      Serial.println("Module N : ");
      Serial.println(g_N);
      g_fi_n = (g_p - 1) * (g_q - 1);
      Serial.println("fi(N): ");
      Serial.println(g_fi_n);
      g_e = generate_pk(g_fi_n);
      Serial.println("Public key");
      Serial.println(g_e);
      g_d = generate_pv_k(g_e, g_fi_n);
      Serial.println("Private key");
      Serial.println(g_d);
      
      g_state = STATE_RECV_PK;
     
      break;
    }
    
    case STATE_RECV_PK:{
      
      Serial.println("RECV PK STATE ");
      
      if (CAN_MSGAVAIL == CAN.checkReceive()) {         // check if data coming
        CAN.readMsgBuf(&g_pk_B_size, g_pk_B);    // read data,  len: data length, buf: data buf

        unsigned long canId = CAN.getCanId();

        Serial.println("-----------------------------");
        Serial.println("get data from ID: 0x");
        Serial.println(canId, HEX);

        for (int i = 0; i < g_pk_B_size; i++) { // print the data
            Serial.println("****************************");
            Serial.println(g_pk_B[i]);
        }
        
        g_ack0[0] = '0';
        CAN.sendMsgBuf(0x01, 0, 1, g_ack0);
        
        g_state = STATE_ENCRYPT;
      }
      else g_state = STATE_RECV_PK;
    
      break;
    }
    
    case STATE_ENCRYPT:{
      
      Serial.println("STATE ENCRYPT");
      
      g_pk_B_int = (g_pk_B[0] << 8) | g_pk_B[1]; 
      Serial.println(g_pk_B_int);
      
      g_encrypted_buffer = encrypt(g_N,g_pk_B_int);
      
      BigNumber::finish();
      
      for(int i = 0; i < 8; i++){
        Serial.println("++++++++++++++");
        Serial.println(g_encrypted_buffer[i]);
      }
      
      g_state = STATE_TX;
      
      break;
    }
    
    case STATE_TX:{

      Serial.println("STATE TX");
      
      CAN.sendMsgBuf(0x02, 0, 8, g_encrypted_buffer);

      g_state = STATE_STOP;
       
      break;
    }

    case STATE_STOP:{
      
      Serial.println("STATE STOP");
      esc++;
      
      g_state = STATE_STOP;
      
      break;
    }

    default:{
      g_state = STATE_STOP;
      Serial.println("STATE error");
    }
  }

  delay(1000);
  
}
