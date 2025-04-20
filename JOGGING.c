#include <xc.h>

// Configuración del PIC16F887
#pragma config FOSC = INTRC_NOCLKOUT
#pragma config WDTE = OFF
#pragma config PWRTE = OFF
#pragma config MCLRE = ON
#pragma config CP = OFF
#pragma config CPD = OFF
#pragma config BOREN = OFF
#pragma config IESO = OFF
#pragma config FCMEN = OFF
#pragma config LVP = OFF

#define _XTAL_FREQ 4000000  // Oscilador a 4MHz

// Definición de pines
#define MODE        RB0  // 1 = Full Step, 0 = Half Step
#define DERECHA_ON  RB1  // 0 = Giro derecha activo
#define IZQUIERDA_ON RB2 // 0 = Giro izquierda activo
#define MEDIA_VUELTA_DERECHA RB3 // 0 = Media vuelta derecha
#define MEDIA_VUELTA_IZQUIERDA RB4 // 0 = Media vuelta izquierda
#define JOG_BUTTON  RB5  // Botón para activar el jogging

// Salidas al L298N
#define IN1 RD0
#define IN2 RD1
#define IN3 RD2
#define IN4 RD3

// Pines para LCD (usando Puerto C)
#define LCD_RS RC0
#define LCD_EN RC1
#define LCD_D4 RC2
#define LCD_D5 RC3
#define LCD_D6 RC4
#define LCD_D7 RC5

// Variables globales
volatile unsigned char timer_flag = 0;
volatile unsigned char media_vuelta_derecha_flag = 0;
volatile unsigned char media_vuelta_izquierda_flag = 0;
volatile unsigned int pasos_restantes = 0;
char lcd_buffer[17]; // Buffer para mensajes LCD

void configPIC() {
    OSCCON = 0b01100010; // IRCF=110 (4MHz), SCS=00 (fuente interna)
    ANSEL = ANSELH = 0;  // Todos los pines como digitales
    TRISB = 0xFF;        // PORTB como entradas
    TRISD = 0x00;        // PORTD como salida
    TRISC = 0x00;        // PORTC como salida (LCD)
    PORTB = PORTD = PORTC = 0x00; // Limpiar puertos
    
    // Configuración Timer0 para 40Hz
    OPTION_REG = 0b00000111;  
    TMR0 = 100;               
    INTCONbits.T0IE = 1;      
    INTCONbits.GIE = 1;       
}

void __interrupt() ISR() {
    if (INTCONbits.T0IF) {
        timer_flag = 1;
        TMR0 = 100;
        INTCONbits.T0IF = 0;
    }
}

// Funciones para el LCD 16x2
void LCD_PulseEnable() {
    LCD_EN = 1;
    __delay_us(1);
    LCD_EN = 0;
    __delay_us(100);
}

void LCD_SendNibble(unsigned char nibble) {
    LCD_D4 = (nibble >> 0) & 1;
    LCD_D5 = (nibble >> 1) & 1;
    LCD_D6 = (nibble >> 2) & 1;
    LCD_D7 = (nibble >> 3) & 1;
    LCD_PulseEnable();
}

void LCD_SendByte(unsigned char byte, unsigned char isData) {
    LCD_RS = isData; // RS=0 para comando, RS=1 para dato
    
    // Enviar nibble alto
    LCD_SendNibble(byte >> 4);
    // Enviar nibble bajo
    LCD_SendNibble(byte & 0x0F);
    
    // Esperar a que el LCD procese el comando/dato
    if(byte == 0x01 || byte == 0x02) // Comandos clear y home necesitan más tiempo
        __delay_ms(2);
    else
        __delay_us(100);
}

void LCD_Init() {
    // Esperar a que el LCD se inicialice
    __delay_ms(15);
    
    // Secuencia de inicialización en modo 4 bits
    LCD_RS = 0;
    LCD_EN = 0;
    
    LCD_SendNibble(0x03);
    __delay_ms(5);
    LCD_SendNibble(0x03);
    __delay_us(100);
    LCD_SendNibble(0x03);
    LCD_SendNibble(0x02); // Cambio a modo 4 bits
    
    // Configuración del LCD
    LCD_SendByte(0x28, 0); // 4 bits, 2 líneas, 5x8 puntos
    LCD_SendByte(0x0C, 0); // Display ON, cursor OFF, blink OFF
    LCD_SendByte(0x06, 0); // Incremento automático del cursor
    LCD_SendByte(0x01, 0); // Limpiar display
    __delay_ms(2);
}

void LCD_Clear() {
    LCD_SendByte(0x01, 0);
    __delay_ms(2);
}

void LCD_SetCursor(unsigned char row, unsigned char col) {
    unsigned char address;
    if(row == 0)
        address = 0x80 + col;
    else
        address = 0xC0 + col;
    LCD_SendByte(address, 0);
}

void LCD_PrintString(const char *str) {
    while(*str) {
        LCD_SendByte(*str++, 1);
    }
}

void LCD_UpdateDisplay() {
    LCD_SetCursor(0, 0);
    
    // Mostrar modo de operación
    if(MODE) {
        LCD_PrintString("Modo: FULL STEP ");
    } else {
        LCD_PrintString("Modo: HALF STEP ");
    }
    
    LCD_SetCursor(1, 0);
    
    // Mostrar estado del motor
    if(media_vuelta_derecha_flag) {
        LCD_PrintString("Media vuelta DER");
    }
    else if(media_vuelta_izquierda_flag) {
        LCD_PrintString("Media vuelta IZQ");
    }
    else if(!DERECHA_ON && IZQUIERDA_ON) { // Giro derecha
        LCD_PrintString("Girando DERECHA ");
    }
    else if(DERECHA_ON && !IZQUIERDA_ON) { // Giro izquierda
        LCD_PrintString("Girando IZQUIER");
    }
    else {
        LCD_PrintString("Motor DETENIDO ");
    }
}

// Funciones del motor
void fullStepDerecha() {
    static unsigned char paso = 0;
    switch(paso) {
        case 0: IN1 = 1; IN2 = 0; IN3 = 1; IN4 = 0; break;
        case 1: IN1 = 0; IN2 = 1; IN3 = 1; IN4 = 0; break;
        case 2: IN1 = 0; IN2 = 1; IN3 = 0; IN4 = 1; break;
        case 3: IN1 = 1; IN2 = 0; IN3 = 0; IN4 = 1; break;
    }
    paso = (paso + 1) % 4;
}

void fullStepIzquierda() {
    static unsigned char paso = 0;
    switch(paso) {
        case 0: IN1 = 1; IN2 = 0; IN3 = 0; IN4 = 1; break;
        case 1: IN1 = 0; IN2 = 1; IN3 = 0; IN4 = 1; break;
        case 2: IN1 = 0; IN2 = 1; IN3 = 1; IN4 = 0; break;
        case 3: IN1 = 1; IN2 = 0; IN3 = 1; IN4 = 0; break;
    }
    paso = (paso + 1) % 4;
}

void halfStepDerecha() {
    static unsigned char paso = 0;
    switch(paso) {
        case 0: IN1 = 1; IN2 = 0; IN3 = 0; IN4 = 0; break;
        case 1: IN1 = 1; IN2 = 0; IN3 = 1; IN4 = 0; break;
        case 2: IN1 = 0; IN2 = 0; IN3 = 1; IN4 = 0; break;
        case 3: IN1 = 0; IN2 = 1; IN3 = 1; IN4 = 0; break;
        case 4: IN1 = 0; IN2 = 1; IN3 = 0; IN4 = 0; break;
        case 5: IN1 = 0; IN2 = 1; IN3 = 0; IN4 = 1; break;
        case 6: IN1 = 0; IN2 = 0; IN3 = 0; IN4 = 1; break;
        case 7: IN1 = 1; IN2 = 0; IN3 = 0; IN4 = 1; break;
    }
    paso = (paso + 1) % 8;
}

void halfStepIzquierda() {
    static unsigned char paso = 0;
    switch(paso) {
        case 0: IN1 = 0; IN2 = 0; IN3 = 0; IN4 = 1; break;
        case 1: IN1 = 0; IN2 = 1; IN3 = 0; IN4 = 1; break;
        case 2: IN1 = 0; IN2 = 1; IN3 = 0; IN4 = 0; break;
        case 3: IN1 = 0; IN2 = 1; IN3 = 1; IN4 = 0; break;
        case 4: IN1 = 0; IN2 = 0; IN3 = 1; IN4 = 0; break;
        case 5: IN1 = 1; IN2 = 0; IN3 = 1; IN4 = 0; break;
        case 6: IN1 = 1; IN2 = 0; IN3 = 0; IN4 = 0; break;
        case 7: IN1 = 1; IN2 = 0; IN3 = 0; IN4 = 1; break;
    }
    paso = (paso + 1) % 8;
}

void Jogging() {
    static unsigned char last_jog_button = 1;  // Estado anterior del botón
    
    unsigned char current_jog_button = JOG_BUTTON;  // Estado actual del botón
    
    // Detectar flanco de bajada (presión del botón)
    if (last_jog_button && !current_jog_button) {
        // Si el motor está en modo Full Step
        if (MODE) {
            if (!DERECHA_ON && IZQUIERDA_ON) {  // Giro a la derecha
                fullStepDerecha();  // Mueve el motor un paso a la derecha
            } else if (DERECHA_ON && !IZQUIERDA_ON) {  // Giro a la izquierda
                fullStepIzquierda();  // Mueve el motor un paso a la izquierda
            }
        }
        // Si el motor está en modo Half Step
        else {
            if (!DERECHA_ON && IZQUIERDA_ON) {  // Giro a la derecha
                halfStepDerecha();  // Mueve el motor un paso a la derecha
            } else if (DERECHA_ON && !IZQUIERDA_ON) {  // Giro a la izquierda
                halfStepIzquierda();  // Mueve el motor un paso a la izquierda
            }
        }
    }
    
    // Actualizar el estado anterior del botón
    last_jog_button = current_jog_button;
}

void main() {
    configPIC();
    LCD_Init();
    
    while(1) {
        Jogging();
        LCD_UpdateDisplay();
    }
}

