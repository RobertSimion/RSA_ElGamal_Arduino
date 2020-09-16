#include <SPI.h>
#include <mcp_can.h>
#include <mcp_can_dfs.h>

#include <stdlib.h>
#include <math.h>
#include "BigNumber.h"

#define SPICSPIN  10
#define SCKPIN 13
#define SIPIN 11
#define SOPIN 12
#define ISRPIN 9

// GND_CAN - POWER GND MCU
// VCC_CAN - POWER 5V MCU

MCP_CAN CAN(SPICSPIN);

void setup(){
  
    Serial.begin(115200);
   
    while (CAN_OK != CAN.begin(CAN_500KBPS,MCP_8MHz)){
        
        Serial.println("CAN BUS Init Failed");
        delay(100);
        
    }
    
    Serial.println("CAN BUS  Init OK!");
    BigNumber::begin ();

}

enum RSA_STATE{
  STATE_UNINIT,
  STATE_INIT_PARAM,
  STATE_SEND_PK,
  STATE_RX,
  STATE_DECRYPT,
  STATE_STOP
};

RSA_STATE g_state = STATE_UNINIT;

unsigned char *g_ack0;
unsigned char *g_ack1;
unsigned char *g_clear;
unsigned char g_len_ack0;
unsigned char g_len = 0;
unsigned char g_cryptogram_len;
unsigned char g_buf[8];
unsigned char g_decrypted[8];
unsigned char g_cryptogram[8];

int g_p,g_q,g_e,g_d,g_N;

unsigned char* decrypt(int N, int d){
  
  unsigned char *decrypted_buffer = (unsigned char*)malloc(8 * sizeof(unsigned char));
  
  BigNumber pvk = BigNumber(d);
  BigNumber n = BigNumber(N);

  for(int i = 0; i < 8; i++){
    BigNumber msg = BigNumber((int)g_decrypted[i]);
    *(decrypted_buffer + i) = msg.powMod(pvk,n);
  }
  
  return decrypted_buffer;
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


  srand(2);
  
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
    
    if(esc)exit(0);
    
    switch(g_state){
      
      case STATE_UNINIT:{
        
        g_state = STATE_INIT_PARAM;
        Serial.println("START state");
        
        break;
      }
    
      case STATE_INIT_PARAM:{
        Serial.println("INIT PARAM state");
      
        g_p = 19;
        Serial.println("p param: ");
        Serial.println(g_p);
        g_q = 13;
        Serial.println("q param: ");
        Serial.println(g_q);
        g_N = g_p * g_q;
        Serial.println("Module N : ");
        Serial.println(g_N);
        int g_fi_n = (g_p - 1) * (g_q - 1);
        Serial.println("fi(N): ");
        Serial.println(g_fi_n);
        g_e = generate_pk(g_fi_n);
        Serial.println("Public key");
        Serial.println(g_e);
        g_d = generate_pv_k(g_e, g_fi_n);
        Serial.println("Private key");
        Serial.println(g_d);

        g_state = STATE_SEND_PK;
        
        break;
      }
    
      case STATE_SEND_PK:{
        
        Serial.println("SEND PK state");
        unsigned char pk_bytes[2];
        
        pk_bytes[0] = (g_e >> 8) & 0xFF;
        pk_bytes[1] =  g_e & 0xFF;
        
        CAN.sendMsgBuf(0x00, 0, sizeof(pk_bytes), pk_bytes);
        
        if (CAN_MSGAVAIL == CAN.checkReceive()){
           CAN.readMsgBuf(&g_len_ack0, g_ack0);
           
           g_state = STATE_RX;  
        }
        else g_state = STATE_SEND_PK;
        
        break;
      }
    
      case STATE_RX:{
        Serial.println("STATE RX:");
        
        if (CAN_MSGAVAIL == CAN.checkReceive()) {         // check if data coming
          CAN.readMsgBuf(&g_cryptogram_len, g_cryptogram);    // read data,  len: data length, buf: data buf

          unsigned long canId = CAN.getCanId();

          Serial.println("-----------------------------");
          Serial.println("get data from ID: 0x");
          Serial.println(canId, HEX);

          for (int i = 0; i < g_cryptogram_len; i++) { // print the data
            Serial.println("****************************");
            Serial.println(g_cryptogram[i]);
            g_decrypted[i] = g_cryptogram[i];
            
          }
          
          g_state = STATE_DECRYPT;
        }
        else g_state = STATE_RX;
        break;
      }
      
      case STATE_DECRYPT:{
        
        Serial.println("STATE DECRYPT");
        
        g_clear = decrypt(g_N, g_d);
        
        BigNumber::finish();
        
        Serial.println("*********");
        Serial.println("Clear text: ");
        for(int i = 0; i < 8; i++){
          
          Serial.print((char)g_clear[i]);
        }
        Serial.println();
        
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
