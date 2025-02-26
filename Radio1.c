
/*  Retro Computing Radio with Axiom 68HC11 */

/* ****   Notes
   I think the sync jumper should be on for the LCD timing, enable will follow E stobe.
   Maybe not, E is the data pin on LCD chipselect decode, try off first for better address and data setup time.
   Works with sync off, so using sync off.
   Had one LCD hiccup.  Trying sync jumper on, works ok.

   have 2 pages in the LCD selected by scrolling left/right by 16
   address = row * $40 + page * 16  + column

   Moved U7 to U6 socket and removed the extra 32k of RAM.  Use U6load to load code into the EEPROM in
   U6.  This keeps the Buffalo monitor in the memory map with the current vectors to page 0.
    Now have 32k of ram segmented by the internal registers at $1000.   0000 to 7fff.
    8k of EEPROM at 8000 to 9fff.   Internal 512 bytes of EEPROM B600 B7ff.
    12k Internal EPROM  D000 to FFFF of which 8k is programmed with the Buffalo monitor start address E000.

   Buffalo writes values to $9800 in search of an I/O device, so this location needs to be avoided.  It is 
   in the new range for the EEPROM starting at 8000 going to 9fff.  Possibly this value will alternate from
   03 to 12 hex as the test fails. No, it stays at 3 - so the EEPROM must return some other value than 
   the one written when a write is busy.  When we had ram at $8000 not sure why this test didn't pass. 
   Removed J6 jumper for now.


   To do:
      take out the scope loop code when done looking. It's commented out now.

**** */

#include "hc11.h"

/* all globals here, 3582 locations from $200 to registers at $1000 */
/* statics don't work as they are inline with code( illegal instructions ), use globals */

#define FOUR_BIT_MODE 0x20
#define EIGHT_BIT_MODE 0x30

#define SI5351 0x60
#define CLK_RX   2
#define CLK_BFO  1
#define CLK_OFF  0
#define PLLA 26
#define PLLB 34
/********  returning to function calls instead of inline macro's
#define ZACC acc0 = acc1 = acc2 = acc3 = acc4 = 0
#define ZARG arg0 = arg1 = arg2 = arg3 = arg4 = 0
#define ZDIVQ divq0 = divq1 = divq2 = divq3 = divq4 = 0
#define COPY_ACC_ARG  arg0 = acc0,  arg1 = acc1, arg2 = acc2, arg3 = acc3, arg4 = acc4
#define COPY_ACC_DIVQ  divq0 = acc0,  divq1 = acc1, divq2 = acc2, divq3 = acc3, divq4 = acc4
#define COPY_DIVQ_ACC  acc0 = divq0, acc1 = divq1, acc2 = divq2, acc3 = divq3, acc4 = divq4
********/
#define TERM 1
#define LCD 2
#define LEFT 0
#define RIGHT 1
#define ADD_BFO 1
#define JUST_THE_BFO 2

/* button states */
#define IDLE 0
#define ARM 1
#define DTDELAY 2
#define DONE 3 
#define TAP  4
#define DTAP 5
#define LONGPRESS 6
#define DBOUNCE 15

char press,nopress;
char sw_state[3];    /* buttons */


char solution[8];    /* si5351 freq solution to send to si5351 */
char lcd_mode;      /* 8 or 4 bit data bus, using 4 bit, but 8 bit mode is used during lcd init */
char lcd_page;

#define USB 0
#define LSB 4
#define CW  8
char sideband;

struct BANDS {      /* we can init a structure but need to access with assembly code */
  char divider;
  long freq;        /* stored little endian */
};


/* have a ROM version of bands to copy to the working RAM version */
/* need dividers for these freq + 11 mhz. Add bfo in calc solution if wanted */
/* straight dividers without IF were 220,112,80,60,42,40,32,30 */
/*  NOTE: change the default values in the ROM version below, not here */
struct BANDS bands[8] = {
   { 60,  3928000  },
   { 42,  7039980  },
   { 36, 10138700  },
   { 30, 14074000  },
   { 26, 18100000  },
   { 24, 21094600  },
   { 22, 24924600  },
   { 20, 28124600  }
};

char band;
char divider;
char step;
char total_nacks;

char en_last;    /* save the previous encoder reading */



/*  ***********  54 bytes available in zero page when using buffalo monitor ********** */

#asm
    ORG $0
#endasm

char freq0, freq1, freq2, freq3;              /* little endian order */
char bfo0, bfo1, bfo2, bfo3;
 
  /* 40 bit math */ 
char divq4, divq3, divq2, divq1, divq0;       /* dividend, quotient, multiplicand */
char acc4, acc3, acc2, acc1, acc0;            /* accumulator, remainder */
char arg4, arg3, arg2, arg1, arg0;            /* argument, 2nd op for add/sub, divider, multiplier */

char r4, r3, r2, r1, r0;               /* temp storage for division remainder during si5351 calculation */
char b4,b3,b2,b1,b0;                   /* temp storage for b during si5351 calculation */


/* ************************* about 33 used ******************************* */

/*
    the X indexed data stack does not use much space, less than one 256 byte page 
    so trying it at 0fff at the top of the global variable space.  It would take 16 nested 
    subroutine calls to use 256 bytes.
*/

#asm
   ORG $1040    * RAM entry point at top of registers, the CB entry point
*  ORG $8000    * or EPROM entry point, use U6load to load program here
*  LDX  #$7ff0  * data stack top of ram, 14 locals, temp, secval space for main(), f0 to ff
   LDX  #$0ff0  * or data stack just below the register space, grows down toward global variables
*  LDS  #$01ff  * machine stack page 1
*  LDAB  #$93    * load option reg  here
*  STAB  $1039
   JSR init
   JSR main      * jump main,  main can loop or return to buffalo
   RTS
#endasm

/* string lits here in ROM( program section )  as they won't disappear on power off */
/* variables here will work when running from RAM but not when running from ROM */

char msg[] = "Hello 68HC11 SBC\r\n";   /* putting this in ROM or eeprom for non-monitor startup */

#define BAND_WIDTH  2900
long bfo_usb_lsb[3] = { 11059200 , 11059200 - BAND_WIDTH, 11059200 - BAND_WIDTH };
    /* USB, LSB, CW  to be copied to bfo variables */

/* default powerup bandstack, make changes here */
struct BANDS rom_bands[8] = {
   { 60,  3928000  },
   { 42,  7038600  },
   { 36, 10138700  },
   { 30, 14095600  },
   { 26, 18104600  },
   { 24, 21094600  },
   { 22, 24924600  },
   { 20, 28124600  }
};


void init(){   /* run once */
   
 /* variables */
   band = 0;
   total_nacks = 0;
   sideband = LSB;
   step = 3;

   serial_init();
   lcd_init( FOUR_BIT_MODE );
   I2init();
   copy_bandstack();

   puts( msg );         /* sign on message */
   crlf();
   lcd_puts( msg );
   delay_int( 5000 );   /* pretend something useful is happening */
   lcd_clear_row( 0 );

 /*  REG[DDRD] = 0x22;  */     /* SS bit 5 portD as output pin, scope loop !!! */ 

   load_vfo_info( band );
   load_bfo_info( sideband );
   si5351_init();

   calc_solution( divider, ADD_BFO );   /* vfo + IF */
   wrt_solution( PLLB );
   wrt_divider( PLLB, divider );

   calc_solution( 60, JUST_THE_BFO );        /* bfo */
   wrt_solution( PLLA );
   wrt_divider( PLLA, 60 );
   clock(CLK_RX+CLK_BFO);

   display_freq( TERM + LCD );
   crlf();
   display_mode();

   encoder();       /* pick up current state */

   interrupt_setup();
 
}


/* !!! debug function */
void disp_solution( ){
char i;

  for( i = 0; i < 8; ++ i)  display_number(TERM, 4, 0, 0, 0, solution[i] );

}


/* copy the rom bandstack to the working bandstack */
void copy_bandstack(){
char *p1;
char *p2;
char i;

   p1 = bands;
   p2 = rom_bands;

   for( i = 0; i < 40; ++i ){
    /* p1[i] = p2[i];  */           /* array form produces interesting asm code */
     *p1++ = *p2++;                 /* pointer method about same asm code */
   }

}



void main(){
char i;
char t;
char job;
char loops;
char loopsh;
int time1;
int time2;


   loopsh = loops = job = i = 0;

for( ; ; ){                     /* loop main */

  /* high priority tasks here, done each loop */

  /* do work each 4 ms */
  if( (REG[TFLG2] & 0x40) == 0x40 ){     /* RTI flag */
     REG[TFLG2] =  0x40;                 /* write one to clear */
     job = 1;
     ++loops;
     if( loops == 0  && loopsh == 30 ) break;   /* !!! (debug timeout) changed exit to a switch press */     
  }

  switch( job ){                         /* do a different task each loop */
                                         /* timed tasks, each has 4ms between task visit */
    /* read switches, encoder, vox, etc */
    case 1:
      t = encoder();
      if( t ) qsy( t, step );
    break;

    case 2:                                 /* analog read, single, mult or scan? */
       REG[ADCTL] = 1;                      /* bit 5 scan, bit 4 mult */
       while( (REG[ADCTL] & 0x80) == 0 );
       t = REG[ADR2];                       /* buttons on PE1, if use mult then other inputs are now valid */
     /*  if( t > 20 ) display_number( TERM,4,0,0,0,t); */
       button_state( t );
    break;

    case 3:                          /* button under the freq display */
       if( sw_state[0] > DONE ){
        /* *** if( sw_state[0] == TAP ) puts("TAP ");
          if( sw_state[0] == DTAP ) puts("DTAP ");
          if( sw_state[0] == LONGPRESS ) puts("LONG "); **** */

          if( sw_state[0] != LONGPRESS ) band_change( sw_state[0] );
          else loopsh = 30;       /* !!! exit on long press of this switch, debug only code */
          sw_state[0] = DONE;
       }
    break;

    case 4:                          /* button under the mode display */
       if( sw_state[1] > DONE ){
          if( sw_state[1] == TAP ) mode_change();
          sw_state[1] = DONE;
       }
    break;

    case 5:                          /* encoder switch */
       if( sw_state[2] > DONE ){
          if( sw_state[2] == TAP ){
             if( --step > 5 ) step = 5;
             cursor_at_step();
          }
          sw_state[2] = DONE;
       }
    break;

  }     /* end switch */

  if( job ){
      if( ++job > 10 ) job = 0;       /* !!! adjust for number of tasks that need timing */
  }


}  /* end main loop */

 
  lcd_goto( 1, 0, 0 );
  lcd_puts("Exit...");
  lcd_goto( 0, 0, 1 );
  lcd_puts("Hidden Msg");
  delay_int( 3000 );
  lcd_show_page( 1 );
  delay_int( 3000 );
  lcd_show_page( 0 );
  delay_int( 3000 );
  lcd_clear_row( 0 );
  display_freq(TERM+LCD);
  calc_solution( divider, ADD_BFO );
  disp_solution();
  crlf();

  tone_on();                   /* interrupts on */
  for( i = 0; i < 10; ++i ){
    qsy( 1, 1 );                  
    display_freq( TERM ); 
    disp_solution();
    crlf();
    delay_int( 2000 );
  }
  tone_off();

  qsy( 255, 2 );
  display_freq(TERM);
  crlf();

  while( (REG[TFLG2] & 0x40) == 0 ); 
  REG[TFLG2] =  0x40;                 
  while( (REG[TFLG2] & 0x40) == 0 );   
  REG[TFLG2] =  0x40;                  
  while( (REG[TFLG2] & 0x40) == 0 ){  
     delay(1);
     putchar('.');
  }
  crlf();

  puts( "Total Nacks ");
  display_number( TERM,3, 0,0,0, total_nacks );
  crlf();

  time1 = REGI[TCNT];
  display_freq( LCD ); 
 /* qsy( 1, 4 );  */
  time2 = REGI[TCNT];
  time2 = time2 - time1;     /* longer than the counter can count for qsy() */
  #asm
     STAA  2,X
     STAB  3,X
  #endasm
  display_number( TERM,6, 0,0, i, t );   crlf();

  t = REG[PORTA];    /* return values to monitor */
  #asm
    TBA
  #endasm
  t = REG[PORTE];


}    /* end main */

void crlf(){

  puts("\r\n");
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

void band_change( char dir ){

   save_vfo_info( band );
   if( dir == TAP ) ++band;
   else --band;

   if( band == 255 ) band = 7;
   if( band >= 8 ) band = 0;

   load_vfo_info( band );
   calc_solution( divider, ADD_BFO );
   wrt_solution( PLLB );
   wrt_divider( PLLB, divider );
   display_freq( LCD );

}

void load_bfo_info( char side ){

   #asm
      LDAB  2,X              *; sideband, 0,4,8 for usb, lsb, cw
      LDY   #bfo_usb_lsb
      ABY
      LDAB  0,Y
      STAB  bfo0
      LDAB  1,Y
      STAB  bfo1
      LDAB  2,Y
      STAB  bfo2
      LDAB  3,Y
      STAB  bfo3
   #endasm

}



void mode_change(){

   sideband = sideband + 4;
   if( sideband > 8 ) sideband = 0;

   load_bfo_info( sideband );

   clock( 0 );
   calc_solution( divider, ADD_BFO );       /* recalc vfo for different sideband */
   wrt_solution( PLLB );
   wrt_divider( PLLB, divider );

   calc_solution( 60, JUST_THE_BFO );        /* bfo */
   wrt_solution( PLLA );
   /* wrt_divider( PLLA, 60 ); should be ok */
   clock(CLK_RX+CLK_BFO);
   display_mode();

}

void display_freq( char dev ){

   if( dev & LCD ) lcd_goto( 0, 0, 0 );
   display_number( dev, 9, freq3, freq2, freq1, freq0 );
   if( dev & LCD ) cursor_at_step();   

}

char mode_str1[] = "USB";      /* backslash zero not working so do it this way */
char mode_str2[] = "LSB";
char mode_str3[] = "CW ";

void display_mode( ){
char *p;


 /*  lcd_puts( &mode_str1[sideband] ); */   /* addresses past mode_str1 into the others */
    /* !!! almost works, saves D reg instead of Y.  String lits are done with D reg */
   p = &mode_str1[sideband];
   lcd_goto( 0, 13, 0 );
   lcd_puts( p );
   cursor_at_step();
}

/* numbers are stored little endian, 68hc11 access is big endian, get one byte at a time */
long decades[9] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000 };

void display_number(char dev, char digits, char n3, char n2, char n1, char n0){
char i;
char leading; 
char c;

   i = digits-1, digits = 0;
   while( i-- ) digits = digits + 4;
   leading = 1;           /* display leading zero's as spaces */
   acc4 = 0; acc3 = n3;  acc2 = n2; acc1 = n1;  acc0 = n0;

/*  branch from here to loop exit is out of range */
   for( i = digits;  ; i = i - 4 ){
      arg4 = 0;
      #asm
        LDAB  8,X                *; var i
        LDY   #decades
        ABY
        LDAB  0,Y
        STAB  arg0
        LDAB  1,Y
        STAB  arg1
        LDAB  2,Y
        STAB  arg2
        LDAB  3,Y
        STAB  arg3
      #endasm
      copy_acc_divq( ); 
      divide();
      c = divq0 + '0';
      if( c != '0' || i == 0 ) leading = 0;
      if( leading ) c = ' ';
      if( dev & LCD ){
          if( i == 20 || i == 8 ) lcd_data( ',' );   /* 14,096,600 formating */
          lcd_data( c );
      }
      if( dev & TERM ) putchar( c );
      if( i == 0 ) break;                   /* above if branch is out of range */
   }

}

void cursor_at_step(){      /* cursor under the tuning step digit */
char i;

   i = step + 1;
   if( step > 2 ) ++i;          /* skip comma in freq format on LCD */
   lcd_goto( 0, 11 - i, 0 );   
 /*   lcd_cursor( LEFT, i );   this function not needed maybe */  

}


void qsy( char dir, char step ){
char i;

   ++dir;            /* dir is -1 or 1,  255 -> 0,  1 -> 2 */

   acc4 = 0, acc3 = freq3, acc2 = freq2, acc1 = freq1, acc0 = freq0;
   i = arg4 = 0;
   while( step-- ) i = i + 4;

   #asm
     LDAB 4,X
     LDY  #decades
     ABY
     LDAB  0,Y
     STAB  arg0
     LDAB  1,Y
     STAB  arg1
     LDAB  2,Y
     STAB  arg2
     LDAB  3,Y
     STAB  arg3
   #endasm
     
   if( dir ) dadd();
   else dsub();
   freq3 = acc3, freq2 = acc2, freq1 = acc1, freq0 = acc0;
   calc_solution( divider, ADD_BFO );
   wrt_solution( PLLB );
   display_freq( LCD );


}

/**********************  40 bit math **********************/

void zacc(){       /* clear acc */

   acc0 = acc1 = acc2 = acc3 = acc4 = 0;
}

void zarg(){       /* clear arg */ 

   arg0 = arg1 = arg2 = arg3 = arg4 = 0;
}

void zdivq(){

   divq0 = divq1 = divq2 = divq3 = divq4 = 0;
}

void copy_acc_arg(){

  /* arg0 = acc0;  arg1 = acc1; arg2 = acc2; arg3 = acc3; arg4 = acc4; */

   arg4 = acc4;
   #asm
     LDD acc3
     STD arg3
     LDD acc1
     STD arg1
   #endasm

}

void copy_acc_divq(){

  /*  divq0 = acc0;  divq1 = acc1; divq2 = acc2; divq3 = acc3; divq4 = acc4; */

   divq4 = acc4;
   #asm
     LDD acc3
     STD divq3
     LDD acc1
     STD divq1
   #endasm


}

void copy_divq_acc(){

  /* acc0 = divq0, acc1 = divq1, acc2 = divq2, acc3 = divq3, acc4 = divq4;  */

   acc4 = divq4;
   #asm
     LDD divq3
     STD acc3
     LDD divq1
     STD acc1
   #endasm

}


/*
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

*/



char dadd(){  /* add the accum and argument, return carry */
char carry;

   carry = 0;
   #asm
      LDD    acc1
      ADDB   arg0     *; could save 1 cycle with ADDD arg1
      ADCA   arg1
      STD    acc1
      LDD    acc3
      ADCB   arg2
      ADCA   arg3
      STD    acc3
      LDAB   acc4
      ADCB   arg4
      bcc   _dadd1
      inc   2,X
_dadd1 
      STAB   acc4
   #endasm

   return carry;

}



/*
   
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
     bcc    _dsub1    ; borrow is carry true 
     inc   2,X
_dsub1
     STAB   acc4
*/


char dsub(){  /* sub the arg from the accum, return borrow */
char borrow;

   borrow = 0;   
   #asm
     LDD    acc1
     SUBB   arg0
     SBCA   arg1
     STD    acc1
     LDD    acc3
     SBCB   arg2
     SBCA   arg3
     STD    acc3    
     LDAB   acc4
     SBCB   arg4
     bcc    _dsub1    ; borrow is carry true 
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

   zacc( );
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
    zacc( );

    while( divq0|divq1|divq2|divq3|divq4 ){
       if( divq0 & 1 ) if( dadd() ) over = 1;
 
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

    return over;
}



void lcd_show_page( char page ){
char i;
char cmd;

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

/* the LCD is wired in 4 bit mode with write tied low */

void lcd_data( char c ){

   EXT_DEV[LCD_DATA] = c;

   if( lcd_mode == FOUR_BIT_MODE ) EXT_DEV[LCD_DATA] = c << 4;
   delay_us( 30 );   /* 40us - call/return load/store */
}

void lcd_command( char c ){

   EXT_DEV[LCD_COMMAND] = c;

   if( lcd_mode == FOUR_BIT_MODE )  EXT_DEV[LCD_COMMAND] = c << 4;
   delay_us( 31 ); 
}

void lcd_goto( char row, char col, char page ){
char adr;

    adr = col;
    if( row ) adr = adr + 0x40;
    if( page ) adr = adr + 16;
    lcd_command( 0x80 + adr );

}

void lcd_clear_row( char row ){
char i;

   lcd_goto( row, 0, lcd_page );
   for( i = 0; i < 16; ++i ) lcd_data(' ');
   lcd_goto(row, 0, lcd_page );   

}


/* *************************** unused for now
void lcd_cursor( char lr, char amt ){    /* shift left or right, 0-left, 1-right 
char cmd;

    cmd = ( lr )? 0x14 : 0x10;
    while( amt-- ) lcd_command( cmd );

}
****************************** */


/* init seq: 3 8 bit mode sets, actual mode set, mode set 2 line, on cursor steady, clear, entry mode */
char lcd_dly[8] = { 30,  5,    1,    0,    0,    0,   0,    10};
char lcd_cmd[8] = {  0x30, 0x30, 0x30, 0xff, 0xfe, 0x0e, 0x01, 0x06 };

void lcd_init( char mode ){
char i;
char dly, cmd;

    
    lcd_mode = EIGHT_BIT_MODE;                                /* start with some 8 bit commands */
    for( i = 0; i < 8; ++i ){
        dly = lcd_dly[i];
        cmd = lcd_cmd[i];
        if( cmd == 0xff ) cmd = mode;                          /* switch to 4 bit maybe */
        if( cmd == 0xfe ) cmd = ( lcd_mode = mode ) + 0x08;    /* now using correct mode */
        delay( dly );
        lcd_command( cmd );
    }
    lcd_page = 0;

}



void delay_int( int ms ){
char reg_a;
char reg_b;

    #asm
      LDD     2,X
      STAA    4,X
      STAB    5,X
    #endasm

    while( reg_a-- ) delay(255);
    delay( reg_b );

}


void delay( char ms ){

    while( ms-- ){
      delay_us(242);
      delay_us(246);
      delay_us(246);
      delay_us(246);
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
/********************
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
******************* unused */


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

    for( i = 0; i < 8; ++i ){
       if( dat & 0x80 ) SDA_HIGH;
       else SDA_LOW;
       SCL_HIGH;
       SCL_LOW;
       dat = dat << 1;
    }
    SDA_HIGH;
    SCL_HIGH;
    nack = REG[PORTD] & 8;
    SCL_LOW;
    if( nack ) total_nacks = total_nacks + 1;
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
   si_write( 17, 0x4c );    /* clock 1 assigned to plla with 2ma drive */
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



void wrt_divider( char pllx, char div ){    

  /* 128 * val - 512, same as (val-4) * 128 */
  /* pllx = pllx + 19;              dividers offset from PLLA or PLLB by 19 */
  /* div = ( div - 4 ) >> 1;        mult by 256 and divide by 2 == mult by 128, here is divide by 2 */
  /* si_write(  pllx, div );        write high byte is mult by 256, dividers are even, low byte is always zero */  

   si_write( pllx + 19, (div-4) >> 1 );   /* all preceding in one command */

   si_write(  177, 0xA0 );                   /* reset pll's */
   delay(5);
   si_write(  177, 0xA0 );                   /* double reset needed ? */
}


/*  pass the PLLA or PLLB to this function also */
void wrt_solution( char pllx ){                  /* loads the PLL information packet */
char k;

   I2start();
   I2send( SI5351 << 1 );
   I2send(pllx);
   for( k = 0; k < 8; ++k ) I2send( solution[k] );
   I2stop();
}




/* Si5351 calculation */
/* char ref_clock[4] = { 0x01, 0x7D, 0x78, 0x40 };   /* 25mhz*/
char ref_clock[4] = { 0x01, 0x7D, 0x78, 0x90 };   /* 25000080 */

void calc_solution( char divider, char bfo_mode ){
char i;
char a;

   /* solution array  solution[ P3h,P3,P1u,P1h,P1,P2u,P2h,P2 ] */
   /* if c is small enough, P1u and P2u will be zero and mashup of P3u bits is avoided */
   /* make c 16384, a value that can be masked with 16383 3fff */
   /* or use a smaller value if overflow in multiply, 8192 mask is 1fff */
   /* solution[0] is 0x40 or 0x20 depending upon 16k or 8k value for c */
   /* fix mask at end of function if this is changed, 3f or 1f */
   solution[0] = 0x40;  solution[1] = 0x00;           /* P3 is c */

   /*  add in the bfo, or use just the bfo.  Set up for adding the bfo to freq */
   arg4 = 0, arg3 = freq3, arg2 = freq2, arg1 = freq1, arg0 = freq0;
   acc4 = 0, acc0 = bfo0, acc1 = bfo1, acc2 = bfo2, acc3 = bfo3;
   if( bfo_mode == JUST_THE_BFO ) zarg();
   else if( bfo_mode != ADD_BFO ) zacc();
   dadd();           /* add bfo or add zero to freq or zero to bfo */
   copy_acc_arg();   /* arg set for following multiply */

   /* pll freq is freq * divider */
   zdivq( );
   divq0 = divider;      /* multiplier */
   multiply();

   /* a is the PLL freq divided by the clock freq */
   copy_acc_divq( );
   arg4 = 0; arg3 = ref_clock[0];  arg2 = ref_clock[1];  arg1 = ref_clock[2];  arg0 = ref_clock[3];
   divide();
   a = divq0;       /* should be a small number, one byte */

   /* remainder is in the acc */

   /* b is r * c / clock */ 
   copy_acc_divq( );
   arg1 = solution[0];  arg0 = solution[1]; arg4 = arg3 = arg2 = 0;
   i= multiply();  
  if( i ) { puts( "multiply carry out " );  putchar( i + '0' ); crlf(); } /* remove when working without error */
   copy_acc_divq( );
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
   
   /* remainder * 128 / clk , 7 more bits of precision */
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
   b0 = b0 | ( divq0 & 127 );
   
 

   /* P1 = 128 * a + 128 * b / c - 512 */
   zdivq( );
   zarg( );
   divq0 = a;
   arg0 = 128;
   multiply();
   solution[2] = acc2, solution[3] = acc1, solution[4] = acc0;   /* save partial calc */
   /* 128 * b / c */
   divq4 = b4, divq3 = b3, divq2 = b2, divq1 = b1, divq0 = b0;   /* load 128*b */
   zarg( );
   arg1 = solution[0], arg0 = solution[1];
   divide();
   copy_divq_acc( );
   arg4 = arg3 = 0, arg2 = solution[2], arg1 = solution[3], arg0 = solution[4];
   dadd();
   zarg( );
   arg1 = 2;    /* 512 */
   dsub();
   solution[2] = acc2, solution[3] = acc1, solution[4] = acc0;
   
   /*   P2 = 128 * b - 128 * b/c * c   */
   /*   mask 128 * b with (c-1) since using power of two for c, skip two mults and a divide */
   /* b2 = 0,  b1 = b1 & 0x3f;  skip intermediate storage of results */      
   /* solution[5] = b2, solution[6] = b1, solution[7] = b0; */

   solution[5] = 0, solution[6] = b1 & 0x3f, solution[7] = b0;   /* 3f or 1f depending up value for c */

   
}


/* just decode 2 states, encoder stops at state 3 when in the detent */
/* 3 1 0 2 3     3 2 0 1 3 */
char encoder(){
char new;
char retval;

   retval = 0;
   new = REG[PORTA] & 3;
   if( new == 3 ){
      if( en_last == 2 ) retval = 255;
      if( en_last == 1 ) retval = 1;
   }
   en_last = new;
   return retval;
}


void _timer2_compare(){      /* 600 hz tone on PA6 */
   
   REGI[TOC2] = REGI[TOC2] + 1667;
   REG[TFLG1] = 0x40;

}       /* RTI */


void interrupt_setup(){
 
  /* REG[TFLG1] = 0x40;  */   /* write one to clear any pending int, not needed here */
   REG[TCTL1] = 0x40;     /* pin toggle  PA6 */

  /* write vector */
  #asm
    LDAB  #$7E      *; JMP
    STAB  $dc
    LDD   #_timer2_compare
    STD   $dd
  #endasm  

}

void tone_on(){

    REGI[TOC2] = REGI[TCNT] + 1667;  /*  1st interrupt in half period */  
    REG[TFLG1] = 0x40;               /* write one to clear any pending int */
    REG[TMSK1] = 0x40;               /* int enable */
    #asm
      CLI
    #endasm 
}

void tone_off(){

   #asm
     SEI
   #endasm
   REG[TMSK1] = 0;
}

void button_state( char val ){   /* state machine running at 4ms rate */
char sw,st,i;
 
      sw = 0;
      if( val > 176 && val < 200 ) sw = 1;
      if( val > 220 && val < 240 ) sw = 2;
      if( val > 240 ) sw = 4;
        
      if( sw ) ++press, nopress= 0;
      else ++nopress, press= 0;
      
      /* switch state machine */
      for( i= 0; ; ++i ){     /* branch out of range */
         st= sw_state[i];      /* temp the array value to save typing */

         if( st == IDLE && (sw & 0x1) && press >= DBOUNCE ) st= ARM;
         if( st == DONE && nopress >= DBOUNCE ) st = IDLE;   /* reset state */

         /* double tap detect */
         if( st == ARM && nopress >= DBOUNCE/2 )     st= DTDELAY;
         if( st == ARM && (sw & 0x1 ) && press >= 8*DBOUNCE )  st= LONGPRESS;
         if( st == DTDELAY && nopress >= 4*DBOUNCE ) st= TAP;
         if( st == DTDELAY && ( sw & 0x1 ) && press >= DBOUNCE )   st= DTAP;
         
         sw_state[i]= st;      
         sw = sw >> 1;   /* next switch */
         if( i == 2 ) break;
      }        
}

/*******   test integer shifts 
void test( int i ){
int j;
char v;
   i = i >> 4;
   #asm
_shift
      LSRD
      DEY
      BNE _shift
   #endasm
   j = v;
   i = i << j;
   return i;
} 
***************/

