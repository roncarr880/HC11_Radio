
/*  Retro Computing Radio with 68HC11 */

/* ****   Notes
   I think the sync jumper should be on for the LCD timing, enable will follow E stobe.
   To do:
      take out the scope loop code on int_delay_us and setup when done looking

**** */

#include "hc11.h"

/* all globals here, 3582 locations from $200 to registers at $1000 */
/* statics don't work as they are inline with code( illegal instructions ), use globals */

#define FOUR_BIT_MODE 0x20
#define EIGHT_BIT_MODE 0x30

#define SI5351 0x60
#define CLK_RX  2
#define CLK_TX  1
#define CLK_OFF 0
#define PLLA 26
#define PLLB 34
#define ZACC acc0 = acc1 = acc2 = acc3 = acc4 = 0
#define ZARG arg0 = arg1 = arg2 = arg3 = arg4 = 0
#define ZDIVQ divq0 = divq1 = divq2 = divq3 = divq4 = 0
#define COPY_ACC_ARG  arg0 = acc0,  arg1 = acc1, arg2 = acc2, arg3 = acc3, arg4 = acc4
#define COPY_ACC_DIVQ  divq0 = acc0,  divq1 = acc1, divq2 = acc2, divq3 = acc3, divq4 = acc4
#define COPY_DIVQ_ACC  acc0 = divq0, acc1 = divq1, acc2 = divq2, acc3 = divq3, acc4 = divq4


char lcd_mode;
char lcd_page;
/* char freq3 = 0x00, freq2 = 0x6B, freq1 = 0x6C, freq0 = 0x00; */ /* long freq word 7.040mhz */
char freq0, freq1, freq2, freq3;              /* little endian order */  
char divq4, divq3, divq2, divq1, divq0;
char acc4, acc3, acc2, acc1, acc0;
char arg4, arg3, arg2, arg1, arg0;
char r4, r3, r2, r1, r0;

char solution[8];    /* si5351 freq solution to send to si5351 */

struct BANDS {      /* we can init a structure but need to access with assembly code */
  char divider;
  long freq;        /* stored little endian */
};


/* this will be a RAM/ROM issue, any var that needs an ititial value will need to be set in function init() */
/* will need a ROM version of bands to copy to the working RAM version */
struct BANDS bands[8] = {
   {220,  3928000  },
   {112,  7074000  },
   { 80, 10138700  },
   { 60, 14074000  },
   { 42, 18100000  },
   { 40, 21094600+1400  },
   { 32, 24924600  },
   { 30, 28124600  }
};

char band = 5;
char divider;

/* could have 2 pages in the LCD selected by scrolling left/right by 16 */
/* 2nd row starts at $40  */
/* address = row * $40 + page * 16  + column */


#asm
  ORG $1040    * entry point at top of registers, the CB entry point
  LDX  #$7ff0    * data stack at top of ram, 14 locals, temp, secval space for main(), f0 to ff
*  LDS  #$01ff  * machine stack at top of page 1
*              * load option reg maybe here
  JSR init
  JMP main
#endasm

/* string lits might be better in ROM( program section )  as they won't disappear on power off */
/* variables here will work when running from RAM but not when running from ROM */
char msg2[] = "Hi once more\r\n";
char msg[] = "Hello 68HC11 SBC\n";   /* putting this in ROM or eeprom for non-monitor startup */





void init(){   /* run once */

   serial_init();
   lcd_init( FOUR_BIT_MODE );
   REG[DDRD] = 0x22;       /* SS bit 5 portD as output pin, scope loop !!! */ 

   load_vfo_info( band );
   si5351_init();
   calc_solution( divider );
   wrt_solution();
   wrt_dividers( divider );
   clock(CLK_RX);

}




void main(){
char i;

  puts( msg );
  puts("\r\n");
  puts( msg2 );

  lcd_puts( msg );       /* function call with pointer */
  lcd_goto( 1, 2, 0 );
  lcd_puts( msg2 );
  lcd_goto( 0, 0, 1 );
  lcd_puts("Hidden Msg");
  delay( 5000, 0 );
  lcd_show_page( 1 );
  delay( 5000, 0 );
  lcd_show_page( 0 );
  display_freq(freq3, freq2, freq1, freq0);
  puts( "\r\n");

  for( i = 0; i < 8; ++ i)
       display_freq( 0, 0, 0, solution[i] );
  puts( "\r\n");


  for( i = 0; i < 20; ++i ){
    calc_solution( divider );
    wrt_solution();
    display_freq( 0,solution[5],solution[6], solution[7] );
    freq0 = freq0 + 10;              
    delay( 5000, 0 );
  }

}


/* need to use some assembly to access the structure bands, read info and store in char variables */ 
void load_vfo_info( char band ){
char offset;

   offset = 0;
   while( band-- ) offset = offset + 5;     /* 5 is size of struct */
   
   #asm
      LDAB  3,X              *; offset
      LDY   #bands
      ABY
      LDAB  0,Y
      STAB  divider
      LDAB  1,Y
      STAB  freq0
      LDAB  2,Y
      STAB  freq1
      LDAB  3,Y
      STAB  freq2
      LDAB  4,Y
      STAB  freq3
   #endasm

}

/* don't want to save the divider, that is fixed per band */
void save_vfo_info( char band ){
char offset;

   offset = 0;
   while( band-- ) offset = offset + 5;

   #asm
     LDAB 3,X
     LDY  #bands
     ABY
     LDAB freq0
     STAB 1,Y
     LDAB freq1
     STAB 2,Y
     LDAB freq2
     STAB 3,Y
     LDAB freq3
     STAB 4,Y
   #endasm

}



/* dividers for decades */

char farg3[8] = { 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
char farg2[8] = { 0xf5, 0x98, 0x0f, 0x01, 0x00, 0x00, 0x00, 0x00 };
char farg1[8] = { 0xe1, 0x96, 0x42, 0x86, 0x27, 0x03, 0x00, 0x00 };
char farg0[8] = { 0x00, 0x80, 0x40, 0xa0, 0x10, 0xe8, 0x64, 0x0a };
/* 989680 f4240 186a0 2710 3e8 64 a 1 dividers */

void display_freq(char f3, char f2, char f1, char f0){
char i;

   
   acc4 = 0; acc3 = f3;  acc2 = f2; acc1 = f1;  acc0 = f0;

   for( i = 0; i < 8; ++i ){
      arg3 = farg3[i];  arg2 = farg2[i];  arg1 = farg1[i];  arg0 = farg0[i];
      arg4 = 0;
      COPY_ACC_DIVQ; 
      divide();
      putchar( divq0 + '0' );
   }

   putchar( acc0 + '0' );
   putchar('\r');
   putchar('\n'); 

}



/**********************  40 bit math **********************/
/* short functions replaced by macros 

zacc(){       /* clear acc 

   acc0 = acc1 = acc2 = acc3 = 0;
}

zarg(){       /* clear arg 

   arg0 = arg1 = arg2 = arg3 = 0;
}

copy_acc_arg(){

   arg0 = acc0;  arg1 = acc1; arg2 = acc2; arg3 = acc3;

}

copy_acc_divq(){

   divq0 = acc0;  divq1 = acc1; divq2 = acc2; divq3 = acc3;

}   ****  */


char dadd(){  /* add the accum and argument, return carry */
char carry;

   carry = 0;
   #asm
      LDAB   acc0
      ADDB   arg0
      STAB   acc0
      LDAB   acc1
      ADCB   arg1
      STAB   acc1
      LDAB   acc2
      ADCB   arg2
      STAB   acc2
      LDAB   acc3
      ADCB   arg3
      STAB   acc3
      LDAB   acc4
      ADCB   arg4
      bcc   _dadd1
      inc   2,X
_dadd1 
      STAB   acc4
   #endasm

   return carry;

}

char dsub(){  /* sub the arg from the accum, return borrow */
char borrow;

   borrow = 0;   
   #asm
     LDAB   acc0
     SUBB   arg0
     STAB   acc0
     LDAB   acc1
     SBCB   arg1
     STAB   acc1
     LDAB   acc2
     SBCB   arg2
     STAB   acc2
     LDAB   acc3
     SBCB   arg3
     STAB   acc3
     LDAB   acc4
     SBCB   arg4
     bcc    _dsub1    ; or bcs ?
     inc   2,X
_dsub1
     STAB   acc4
   #endasm

   return borrow;       /* return borrow as a true value */
}



/* a 40 bit divide algorithm */
/* dividend quotient has the dividend, argument has the divisor, remainder in accumulator */
/* shift upper bits of the dividend into the remainder, test a subtraction of the divisor */
/* restore the remainder on borrow, or set a bit in quotient if no borrow */
/* divisor remains unchanged */

divide(){
char divi;

   ZACC;
   for( divi = 0; divi < 40; ++divi ){
      #asm
        CLC
        ROL   divq0
        ROL   divq1
        ROL   divq2
        ROL   divq3
        ROL   divq4
        ROL   acc0
        ROL   acc1
        ROL   acc2
        ROL   acc3
        ROL   acc4
      #endasm
      if( dsub() ) dadd();     /* borrow, add back */
      else divq0 = divq0 | 1;  /* no borrow, so set a 1 in quotient */
   }
}


/*  A multiply algorithm */
/* 
   arg has multiplicand
   divq has multiplier
   product in acc
   add increasing powers of 2 of the multiplicand to the product depending upon bits set in the multiplier
*/
char multiply( ){
char over;

    over = 0;
    ZACC;

/* puts("Here in mult\n"); */

    while( divq0|divq1|divq2|divq3|divq4 ){
       if( divq0 & 1 ) if( dadd() ) over = 1;    /* over + dadd();*/
 
       #asm
         CLC
         ROR divq4
         ROR divq3
         ROR divq2
         ROR divq1
         ROR divq0

         CLC
         ROL arg0
         ROL arg1
         ROL arg2
         ROL arg3
         ROL arg4
       #endasm
    }

/* puts("Done\n"); */

    return over;
}



void lcd_show_page( char page ){
char i;
char cmd;

  /* page &= 1; */      /*!!! errors */
   page = page & 1;
   if( lcd_page == page ) return;
   cmd = ( page == 1 )? 0x18 : 0x1c;
   for( i = 0; i < 16; ++i ) lcd_command( cmd );
   lcd_page = page;

}


void lcd_puts( char *p ){
char c;

  while( c = *p++ ){
    if( c >= ' ' )  lcd_data( c );
  }

}

/* LCD wired in 4 bit mode with write tied low */
/* a call to here takes 16 us, so we won't be back here for at least that amount of time */

void lcd_data( char c ){

   EXT_DEV[LCD_DATA] = c;

   if( lcd_mode == FOUR_BIT_MODE ) EXT_DEV[LCD_DATA] = c << 4;
   delay_us( 24 );    /* could be as short as 8 I think */
}

void lcd_command( char c ){

   EXT_DEV[LCD_COMMAND] = c;

   if( lcd_mode == FOUR_BIT_MODE )  EXT_DEV[LCD_COMMAND] = c << 4;
   delay_us( 24 ); 
}

void lcd_goto( char row, char col, char page ){
char adr;

    adr = col;
    if( row ) adr = adr + 0x40;
    if( page ) adr = adr + 16;
    lcd_command( 0x80 + adr );

}


/* init seq: 3 8 bit mode sets, actual mode set, mode set 2 line, on, clear, entry mode */
char lcd_dly[8] = { 30,  5,    1,    0,    0,    0,   0,    10};
char lcd_cmd[8] = {  0x30, 0x30, 0x30, 0xff, 0xfe, 0x0c, 0x01, 0x06 };

void lcd_init( char mode ){
char i;
char dly, cmd;

    
    lcd_mode = EIGHT_BIT_MODE;                                /* start with some 8 bit commands */
    for( i = 0; i < 8; ++i ){
        dly = lcd_dly[i];
        cmd = lcd_cmd[i];
        delay( dly, 0 );
        if( cmd == 0xff ) cmd = mode;                          /* switch to 4 bit maybe */
        if( cmd == 0xfe ) cmd = ( lcd_mode = mode ) + 0x08;    /* now using correct mode */
        lcd_command( cmd );
    }
    lcd_page = 0;

}

/* compiler is a char type only for the most part */
/* this function will have a char/int argument issue for constants, just avoid delays of power of two above 255 */
/* issue being delay( 40 ) would store the argument 40 as a char, so it ends up as the high byte 
   and delay will be 10 seconds( 40 * 256 ), so will fix this issue by testing low byte for zero */
   
/* max delay is about 64 seconds, don't use power of two delays above 255, ie 256 512 1024 2048... */
/* zero arg is to avoid using stale stack data if a char is loaded, what a hack this is */
void delay( int ms, char zero ){
char i;

   #asm
     LDD  2,X
     TSTB
     bne _delay1
     TAB               ; xfer less than 255 delay to correct byte
     CLRA
_delay1:
     STD  2,X
   #endasm

   while( ms ){
        REG[PORTD] = REG[PORTD] | 0x20;               /* scope trigger for observing delay timing */
      for( i = 0; i < 4; ++i ) delay_us( 250 );
        REG[PORTD] = REG[PORTD] & ( 0x20 ^ 0xff );
      #asm
         LDY 2,X     ; dec ms
         DEY
         STY 2,X
      #endasm
   }
    

}


void delay_us( char us ){

  #asm
    ldab  2,X
    lsrb
    lsrb
    incb               * in case shift result is zero
_delay_us1
*    ldaa  0            * 3   8 cycle loop, with dummy load of reg a from zero page, total 4 us
    nop
    decb               * 2
    bne   _delay_us1   * 3
  #endasm

}


/* reverse the bits in a byte */
/* for using SPI to update a DDS for example */
char reverse_byte( char dat ){
char dest,i;

   for( i = 0; i < 8; ++i ){
     #asm
        LSR 2,X
        ROL 3,X
     #endasm
   }

   return dest;
}


/* ******  Terminal functions  ****** */

void serial_init(){

   REG[BAUD] = 0x30;     /* 9600 baud, N,8,1 */
   REG[SCCR1] = 0;
   REG[SCCR2] = 0x0C;

}

void puts( char *p ){
char c;
 
   while( c = *p++ ) putchar( c );

}

void putchar( char val ){

   while( ( REG[SCSR] & 0x80 ) != 0x80 );    /* wait ready */
   REG[SCDAT] = val;

}

/* ********************** */


/*   *************  I2C  ************   */

/*     open drain version *****
#define SDA_LOW REG[PORTD] = REG[PORTD] & ( 0xff ^ 8 )
#define SDA_HIGH REG[PORTD] = REG[PORTD] | 8
#define SCL_LOW REG[PORTD] = REG[PORTD] & ( 0xff ^ 0x10 )
#define SCL_HIGH REG[PORTD] = REG[PORTD] | 0x10
******* */
/*  DDRD version - logic inverted - drive low only - high bit in DDRD enables output */
#define SDA_LOW REG[DDRD] = REG[DDRD] | 8
#define SDA_HIGH REG[DDRD] = REG[DDRD] & (0xff ^ 8)
#define SCL_LOW REG[DDRD] = REG[DDRD] | 0x10
#define SCL_HIGH REG[DDRD] = REG[DDRD] & (0xff ^ 0x10)

void I2init(){
   /* set tristate-open drain portD, init pins data direction */
   /* or if using the DDRD to tristate pins, nothing to do here */

}

void I2start(){

/* for DDRD version, make pins drive low, remove this line for open drain PORTD version */
    REG[PORTD] = REG[PORTD] & ( 0xff ^ 0x18 );  /* port set to drive two pins low */

    SDA_LOW;
    SCL_LOW;
}

void I2stop(){

    SDA_LOW;
    SCL_HIGH;
    SDA_HIGH;
}

char I2send( char dat ){
char nack;
char i;
char mask;

    for( i = 0; i < 8; ++i ){
       if( dat & 0x80 ) SDA_HIGH;
       else SDA_LOW;

/* alt method:  mask = (dat & 0x80 )? 0xff : 0xff^8;
                REG[PORTD] = (REG[PORTD] | 0x8) & mask;
  is 1 instruction longer */

       SCL_HIGH;
       SCL_LOW;
       dat = dat << 1;
    }
    SDA_HIGH;
    SCL_HIGH;
    nack = REG[PORTD] & 8;
    SCL_LOW;
    return nack;     /* high is nack, low is ack */

}


void si5351_init(){
char si_adr;

   clock(CLK_OFF);

   for( si_adr = 16; si_adr < 24; ++si_adr ) si_write( si_adr, 0x80 );     /* power down all outputs */

   for( si_adr = 42; si_adr <= 49; ++si_adr ){
      si_write( si_adr, 0x00 );     /* dividers that don't change */
      si_write( si_adr + 8, 0x00);
   }
   si_write( 43, 1 );
   si_write( 43+8, 1 );
   si_write( 16, 0x6c );    /* clock 0 assigned to pllb with 2ma drive */
   si_write( 17, 0x6c );    /* clock 1 assigned to pllb with 2ma drive */
}

void si_write( char reg,  char val){          /* single si5351 write register */ 

   I2start();
   I2send( SI5351 << 1 );
   I2send( reg );
   I2send( val );
   I2stop( );
}

void clock( char val ){            /* control si5351 clocks, CLK_RX, CLK_TX, CLK_OFF */

   si_write( 3, val ^ 0xff );
}

void wrt_dividers( char div ){      /* 128 * val - 512, same as (val-4) * 128 */

   div = ( div - 4 ) >> 1;
                                       /* mult by 256 and divide by 2 == mult by 128 */
   si_write(  45, div );               /* write high bytes for both clocks, low byte unchanged? */
   si_write(  45+8, div );
                                      /* reset pll's */
   si_write(  177, 0xA0 );
   delay(5, 0);
   si_write(  177, 0xA0 );                   /* double reset needed ? */
}

void wrt_solution( ){                  /* loads the PLL information packet */
char k;

   I2start();
   I2send( SI5351 << 1 );
   I2send(PLLB);
   for( k = 0; k < 8; ++k ) I2send( solution[k] );
   I2stop();
}



/* char ref_clock[4] = { 0x01, 0x7D, 0x78, 0x40 };   /* 25mhz*/
char ref_clock[4] = { 0x01, 0x7D, 0x78, 0x90 };   /* 25000080 */

void calc_solution( char divider ){
char i;
char b4,b3,b2,b1,b0;
char a;


     /* clock was 25001740 , 01 7D 7F 0C */

   /* make c 16384, a value that can be masked with 16383 3fff */
   /* try a different value, 8192 mask is 1fff */
   solution[0] = 0x20;  solution[1] = 0x00; 

   /* pll freq is freq * divider */
   ZDIVQ;
   divq0 = divider;      /* multiplier */
   arg4 = 0, arg3 = freq3, arg2 = freq2, arg1 = freq1, arg0 = freq0;
   i = multiply();

   /* a is the PLL freq divided by the clock freq */
   COPY_ACC_DIVQ;
   arg4 = 0; arg3 = ref_clock[0];  arg2 = ref_clock[1];  arg1 = ref_clock[2];  arg0 = ref_clock[3];
   divide();
   a = divq0;       /* should be a small number, one byte */

   /* remainder is in the acc */

   /* b is r * c / clock */ 
   COPY_ACC_DIVQ;
   arg1 = solution[0];  arg0 = solution[1]; arg4 = arg3 = arg2 = 0;
   i= multiply();  
  if( i ) { puts( "overflow in multiply " );  putchar( i + '0' ); puts( "\r\n" ); }
   COPY_ACC_DIVQ;
   arg4 = 0; arg3 = ref_clock[0];  arg2 = ref_clock[1];  arg1 = ref_clock[2];  arg0 = ref_clock[3]; 
   divide();
   r4 = acc4, r3 = acc3, r2 = acc2, r1 = acc1, r0 = acc0;     /* save remainder */

   /* save b * 128, or 256*b >> 1 */
   #asm
      CLC
       ROR divq4
       ROR divq3
       ROR divq2
       ROR divq1
       ROR divq0
       ROR acc4
   #endasm
   b4 = divq3, b3 = divq2, b2 = divq1, b1 = divq0, b0 = acc4 & 0x80;
   
   /* remainder * 128 / clk , 2nd power term? */
   divq4 = r3, divq3 = r2, divq2 = r1, divq1 = r0;  divq0 = 0;  /* (256 * r) >> 1 */
   #asm
     CLC
       ROR divq4
       ROR divq3
       ROR divq2
       ROR divq1
       ROR divq0
   #endasm
   /* arg still has the clock, divide by clock again */
   divide();
  /* r0 = divq0; */  /* or can we do this here */
   b0 = b0 | ( divq0 & 127 );
   
 

   /* P1 = 128 * a + 128 * b / c - 512 */
   ZDIVQ;
   divq0 = a;
   ZARG;
   arg0 = 128;
   multiply();
   solution[2] = acc2, solution[3] = acc1, solution[4] = acc0;   /* save partial calc */
   /* 128 * b / c */
   divq4 = b4, divq3 = b3, divq2 = b2, divq1 = b1, divq0 = b0;
   ZARG;
   arg1 = solution[0], arg0 = solution[1];
   divide();
   COPY_DIVQ_ACC;
   arg4 = arg3 = 0, arg2 = solution[2], arg1 = solution[3], arg0 = solution[4];
   dadd();
   ZARG;
   arg1 = 2;    /* 512 */
   dsub();
   solution[2] = acc2, solution[3] = acc1, solution[4] = acc0;
   
   /*   P2 = 128 * b - 128 * b/c * c   */
   /*   mask 128 * b with (c-1) since using power of two for c, skip two mults and a divide */
   b2 = 0,  b1 = b1 & 0x1f;
   solution[5] = b2, solution[6] = b1, solution[7] = b0;

/* return; */

   
}


