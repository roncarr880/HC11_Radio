

/*
   put data starting at $200, put data stack at $7fff, start program at $1040,
   and if running in rom put machine stack at $01ff

   NO static variables, they end up in ROM and inline as instructions in ram
*/

#asm
  ORG $1000     * internal registers
#endasm

char REG[];     /* REG[PORTE] accesses the PORTE register */
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



#asm
  ORG $B580     * Peripheral Area, 8 decoded stobes covering 16 locations each.  CS0 - 0, CS1 - 16, CS2 - 32
#endasm

char EXT_DEV[];
#define LCD_COMMAND 112
#define LCD_DATA 113

#asm
  ORG $200    * globals area
#endasm
char dv;



