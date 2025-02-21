

/*
   put data starting at $200, put data stack at $7fff, start program at $1040,
   and if running in rom put machine stack at $01ff

   NO static variables, they end up in ROM and inline as instructions in ram
*/

#asm
  ORG $1000     * internal registers
#endasm

char REG[];     /* REG[PORTE] accesses the PORTE register */

/*
#define PORTD  0x8
#define DDRD   0x9
#define PORTE  0xa
#define TMSK2  0x24
#define TFLG2  0x25
#define BAUD   0x2B
#define SCCR1  0x2C
#define SCCR2  0x2D
#define SCSR   0x2E
#define SCDAT  0x2F
#define CONFIG 0x3F
*/
/* register list from GNU headers */
#define PORTA   0x00
#define _RES1   0x01
#define PIOC    0x02
#define PORTC   0x03
#define PORTB   0x04
#define PORTCL  0x05
#define _RES6   0x06
#define DDRC    0x07
#define PORTD   0x08
#define DDRD    0x09
#define PORTE   0x0A
#define CFORC   0x0B
#define OC1M    0x0C
#define OC1D    0x0D
#define TCTN    0x0E
#define TCTN_H  0x0E
#define TCTN_L  0x0F
#define TIC1    0x10
#define TIC1_H  0x10
#define TIC1_L  0x11
#define TIC2    0x12
#define TIC2_H  0x12
#define TIC2_L  0x13
#define TIC3    0x14
#define TIC3_H  0x14
#define TIC3_L  0x15
#define TOC1    0x16
#define TOC1_H  0x16
#define TOC1_L  0x17
#define TOC2    0x18
#define TOC2_H  0x18
#define TOC2_L  0x19
#define TOC3    0x1A
#define TOC3_H  0x1A
#define TOC3_L  0x1B
#define TOC4    0x1C
#define TOC4_H  0x1C
#define TOC4_L  0x1D
#define TOC5    0x1E
#define TOC5_H  0x1E
#define TOC5_L  0x1F
#define TCTL1   0x20
#define TCTL2   0x21
#define TMSK1   0x22
#define TFLG1   0x23
#define TMSK2   0x24
#define TFLG2   0x25
#define PACTL   0x26
#define PACNT   0x27
#define SPCR    0x28
#define SPSR    0x29
#define SPDR    0x2A
#define BAUD    0x2B
#define SCCR1   0x2C
#define SCCR2   0x2D
#define SCSR    0x2E
#define SCDR    0x2F
#define SCDAT   0x2F
#define ADCTL   0x30
#define ADR1    0x31
#define ADR2    0x32
#define ADR3    0x33
#define ADR4    0x34
#define _RES35  0x35
#define _RES36  0x36
#define _RES37  0x37
#define _RES38  0x38
#define OPTION  0x39
#define COPRST  0x3A
#define PPROG   0x3B
#define HPRIO   0x3C
#define INIT    0x3D
#define TEST1   0x3E
#define CONFIG  0x3F




#asm
  ORG $B580     * Peripheral Area, 8 decoded stobes covering 16 locations each.  CS0 - 0, CS1 - 16, CS2 - 32
#endasm

char EXT_DEV[];
#define LCD_COMMAND 112
#define LCD_DATA 113

#asm
  ORG $200    * globals area
#endasm
int _tempY;


