* "Trekkin" apple //e panel client
* Expects an Uthernet ][ card in slot 3

 ORG $2000

F80COL EQU $C300 ; 80 col firmware entrypoint
HOME EQU $FC58 ; clear screen
VTAB EQU $FC22 ; position cursor vertically
COUT EQU $FDED ; output character
WAIT EQU $FCA8 ; delay for a while
BELL EQU $FF3A ; beep

KBD EQU $C000 ; keyboard data
KBDSTRB EQU $C010 ; keyboard stobe
TEXTON EQU $C051 ; enable text mode
SET80STO EQU $C001 ; enable 80 column baking
SET80COL EQU $C00D ; enable 80 column mode
PAGE2ON EQU $C055 ; select page2 for $400
PAGE2OFF EQU $C054 ; select page1 for $400

* this program's zero page regs
PTR EQU $06 ; 2 byte generic ptr
COUNT EQU $08 ; 2 byte counter
SLOG EQU $0A ; 2 byte logic sock addr
SPHY EQU $0C ; 2 byte physical sock addr
COL EQU $19
ROW EQU $1A
KEY EQU $1B

* firmware regs
OURCH EQU $57B ; horizontal cursor pos
CV EQU $25 ; vertical cursor pos

* program entry point
RESET JSR NINIT ; initialize network
 BNE NETERR
 JSR SENDAN ; announce controls
 JSR DINIT ; initialize display
 JMP MAIN
* when there is a network error, wait then reset
NETERR JSR HOME
 LDA #63
 STA COUNT
:WAIT LDA #255 ; about 10 sec
 JSR WAIT
 DEC COUNT
 BNE :WAIT
 JMP RESET

* other files
 USE W5100
 PUT CONFIG
 PUT NET

* set up video and draw screen
DINIT LDA #$A0 ; space
 JSR F80COL ; init 80 column firmware
 STA TEXTON ; more 80 column stuff
 STA PAGE2OFF
 STA SET80COL
 STA SET80STO
* redraw screen
DRAW JSR DBOX
 JSR DSTATUS
 JSR DDISP
 JSR DPROG
 JSR DHULL
 JSR DPOWER
 JSR DTURBO
 JSR DFIELD
 JSR DLOGIC
 JSR DRIVER
 JSR DDRIVE
 JSR DGIBSON
 JSR DSECGRID
 JSR DXREG
 JSR DWPROT
 JSR DBAUD
 JSR DIRQ
 JSR DEDITOR
 JSR DCIPHER
 JSR DDEFRAG
 JSR DJAVA
 JSR DMODEM
 JSR DSNAKE
 RTS

* status line showing what is happening in the game
STATUS LUP 78
 INV " "
 --^
* display line showing a message about the game state
DISP LUP 78
 INV " "
 --^
* timer for current command
PROGRESS DB 0
* starship hull integrity
HULL DB 100

* draw status message
DSTATUS LDA #1
 STA CV
 JSR VTAB
 LDA #1
 STA OURCH
 LDX #0
:CH LDA STATUS,X
 JSR COUT
 INX
 CPX #78
 BNE :CH
 RTS

* draw display message
DDISP LDA #2
 STA CV
 JSR VTAB
 LDA #1
 STA OURCH
 LDX #0
:CH LDA DISP,X
 JSR COUT
 INX
 CPX #78
 BNE :CH
 RTS

* draw progress
DPROG LDX #0
 JSR DMESG
 DB 42,4,3
 ASC "                    ",00
 ASC "@ @ @ @ @ @ @ @ @ @ ",00
 ASC !" " " " " " " " " "!,00
 LDA PROGRESS
 STA COUNT
 LDX #42
:LIT LDA COUNT
 SEC
 SBC #10
 STA COUNT
 BCC :OUT
 LDY #4
 LDA #","
 JSR DAT
 LDY #5
 LDA #'@'
 JSR DAT
 INX
 INX
 BNE :LIT
:OUT RTS

* draw hull integrity
DHULL LDX #0
 LDA HULL
 CMP #100
 BCC :TENS
 LDA #"1"
 STA :PCT
 LDA #"0"
 STA :PCT+1
 STA :PCT+2
 LDA #"%"
 STA :PCT+3
 BNE :DRAW ; always taken
:TENS CMP #10
 BCC :ONES
 LDY #"0"
:DIV10 INY
 SEC
 SBC #10
 CMP #10
 BCS :DIV10
 STY :PCT
 INX
:ONES ADC #"0"
 STA :PCT,X
 LDA #"%"
 STA :PCT+1,X
 LDA #"-"
 STA :PCT+2,X
 STA :PCT+3,X
:DRAW LDX #0
 JSR DMESG
 DB 68,23,1
 ASC "Hull: "
:PCT ASC "-----",00
 RTS

* draw a at row y, column x
DAT PHA  ; save char to put
 STX OURCH
 STY CV
 JSR VTAB
 PLA  ; restore char to put
 JSR COUT
 RTS

* draw a list of text lines
* x register contains control value or #00 for n/a
*   if x is nonzero, use to draw ">" next to state
* parameter block follows JSR
* - x,y,#lines
* - nul-terminated strings (* #lines)
DMESG PLA  ; point to parameter block
 STA PTR
 PLA
 STA PTR+1
 TXA
 PHA  ; save control value
 LDY #1 ; JSR pushes addr-1
 LDA (PTR),Y ; read start column
 STA OURCH
 STA COL
 INY
 LDA (PTR),Y ; read start row
 STA CV
 STA ROW
 JSR VTAB ; cursor to start row
 INY
 LDA (PTR),Y
 STA COUNT ; count lines to draw
:CH INY
 LDA (PTR),Y
 BEQ :CR ; nul terminator?
 JSR COUT
 BNE :CH ; always taken
:CR DEC COUNT
 BEQ :CTRL
 LDA #$8D ; carriage return
 JSR COUT
 LDA COL
 STA OURCH
 BNE :CH ; next line
:CTRL PLA  ; read control value
 BEQ :RET
 AND #$F ; mask row
 SEC
 ADC ROW ; get row+1
 STA CV
 JSR VTAB ; position cursor in line
 INC COL
 LDA COL
 STA OURCH ; position cursor at col
 LDA #">"
 JSR COUT
:RET INY  ; return to caller
 TYA
 CLC
 ADC PTR
 STA PTR
 LDA PTR+1
 ADC #0
 STA PTR+1
 JMP (PTR)

* clear the screen and draw a border box
DBOX JSR HOME
 LDA #"."
 JSR COUT
 JSR :HLIN
 LDA #"."
 JSR COUT
 LDX #22
:SIDES LDA #0
 STA OURCH
 LDA #$FC
 JSR COUT
 LDA #79
 STA OURCH
 LDA #$FC
 JSR COUT
 DEX
 BNE :SIDES
 LDA #"'"
 JSR COUT
 JSR :HLIN
 RTS
:HLIN LDX #78
:HLIN2 LDA #"-"
 JSR COUT
 DEX
 BNE :HLIN2
 RTS

* test if key matches and jump to handler if so
DOKEY MAC
 CMP ]1
 BNE ]NOPE
 JMP ]2
]NOPE
 <<<

* active control from key in A
ACTIVATE CMP #$E1
 BCC :NOLC
 CMP #$FA
 BCS :NOLC
 AND #$DF ; to uppercase
:NOLC
 DOKEY #"P";APOWER
 DOKEY #"T";ATURBO
 DOKEY #"F";AFIELD
 DOKEY #"L";ALOGIC
 DOKEY #"R";ARIVER
 DOKEY #"A";ADRIVE
 DOKEY #"G";AGIBSON
 DOKEY #"S";ASECGRID
 DOKEY #"X";AXREG
 DOKEY #"W";AWPROT
 DOKEY #"B";ABAUD
 DOKEY #"I";AIRQ
 DOKEY #"E";AEDITOR
 DOKEY #"C";ACIPHER
 DOKEY #"D";ADEFRAG
 DOKEY #"J";AJAVA
 CMP #"0"
 BCC :NOMODEM
 CMP #"9"
 BEQ :MODEM
 BCS :NOMODEM
:MODEM JMP AMODEM
:NOMODEM CMP #$88
 BNE :NOL
 JMP SNLEFT
:NOL CMP #$8A
 BNE :NOD
 JMP SNDOWN
:NOD CMP #$8B
 BNE :NOU
 JMP SNUP
:NOU CMP #$95
 BNE :NOR
 JMP SNRIGHT
:NOR RTS

APOWER LDA POWER
 EOR #1
 STA POWER
 LDX #'P'
 JSR SENDST
DPOWER LDX POWER
 JSR DMESG
 DB 2,4,3
 ASC "(P)ower",00
 ASC "  less",00
 ASC "  more",00
 RTS

ATURBO LDA TURBO
 EOR #1
 STA TURBO
 LDX #'T'
 JSR SENDST
DTURBO LDX TURBO
 JSR DMESG
 DB 11,4,3
 ASC "(T)urbo",00
 ASC "  16 MHz",00
 ASC "  33 MHz",00
 RTS

AFIELD INC FIELD
 LDA FIELD
 CMP #'8'
 BNE :SEND
 LDA #'0'
 STA FIELD
:SEND LDX #'F'
 JSR SENDST
DFIELD LDX #0
 JSR DMESG
 DB 20,4,4
 ASC "(F)ield",00
 ASC "    N   ",00
 ASC "  W   E ",00
 ASC "    S   ",00
 LDA FIELD
 AND #7
 TAX
 LDA :CURST,X
 BNE :CURS ; always taken
:CURST ASC "^/>\v/<\"
:CURS LDX #24
 LDY #6
 JSR DAT
 RTS

ALOGIC LDA LOGIC
 EOR #1
 STA LOGIC
 LDX #'L'
 JSR SENDST
DLOGIC LDX LOGIC
 JSR DMESG
 DB 29,4,3
 ASC "(L)ogic",00
 ASC "  clear",00
 ASC "  fuzzy",00
 RTS

ARIVER INC RIVER
 LDA RIVER
 CMP #'3'
 BNE :SEND
 LDA #'0'
 STA RIVER
:SEND LDX #'R'
 JSR SENDST
DRIVER LDX RIVER
 JSR DMESG
 DB 2,19,4
 ASC "(R)iver",00
 ASC "  ford",00
 ASC "  float wagon",00
 ASC "  pay indian",00
 RTS

ADRIVE INC DRIVE
 LDA DRIVE
 CMP #'3'
 BNE :SEND
 LDA #'0'
 STA DRIVE
:SEND LDX #'A'
 JSR SENDST
DDRIVE LDX DRIVE
 JSR DMESG
 DB 16,19,4
 ASC "(A):drive",00
 ASC "  abort",00
 ASC "  retry",00
 ASC "  fail",00
 RTS

AGIBSON LDA #'1'
 CMP SECGRID ; is security grid accessed?
 BNE :FW
 STA GIBSON ; if yes, hack the gibson!
 LDX #'G'
 JSR SENDST
 LDA #30
 STA TGIBSON
 BNE DGIBSON ; always taken
:FW LDA #'F' ; else activate firewall
 STA SECGRID
 LDX #'S'
 JSR SENDST
 LDA #30
 STA TSECGRID
 JSR DSECGRID ; redraw security grid
DGIBSON LDX #0
 LDA #'1'
 CMP GIBSON
 BEQ :HACKED
 JSR DMESG
 DB 2,17,1
 ASC "(G)ibson  ",00
 RTS
:HACKED JSR DMESG
 DB 2,17,1
 ASC "(G)1b50/\/",00
 RTS

ASECGRID JSR RANDOM
 BIT RNG
 BPL :MAGIC
 LDA #'U'
 BNE :UPD ; always taken
:MAGIC LDA #'1'
:UPD STA SECGRID
 LDX #'S'
 JSR SENDST
 LDA #30
 STA TSECGRID
DSECGRID LDX #0
 LDA SECGRID
 CMP #'U'
 BEQ :UHUHUH
 CMP #'F'
 BNE :T1
 JMP :FW
:T1 CMP #'1'
 BNE :OFF
 JMP :ACTIVE
:OFF JSR DMESG
 DB 2,13,4
 ASC "(S)ecurity grid ",00
 ASC "    /_/_/_/_/   ",00
 ASC "   /_/_/_/_/    ",00
 ASC "  /_/_/_/_/     ",00
 RTS
:UHUHUH JSR DMESG
 DB 2,13,4
 ASC "(S)ecurity grid ",00
 ASC "(UH UH UH! YOU )",00
 ASC "(DIDN'T SAY THE)",00
 ASC "(MAGIC WORD... )",00
 RTS
:ACTIVE JSR DMESG
 DB 2,13,4
 ASC "(S)ecurity grid ",00
 ASC "    /_/_/_/_/   ",00
 ASC "   ACCESSED     ",00
 ASC "  /_/_/_/_/     ",00
 RTS
:FW JSR DMESG
 DB 2,13,4
 ASC "(S)ecurity grid ",00
 ASC "    /_/_/_/_/   ",00
 ASC "  (firewall)    ",00
 ASC "  /_/_/_/_/     ",00
 RTS

AXREG INC XREG
 LDA XREG
 CMP #'D'
 BNE :SEND
 LDA #'0'
 STA XREG
:SEND LDX #'X'
 JSR SENDST
DXREG LDX #0
 JSR DMESG
 DB 64,8,13
 ASC "(X)-Register",00
 ASC "  .---.---. ",00
 ASC "  | o | o | ",00
 ASC "  | o | o | ",00
 ASC "  |   | o | ",00
 ASC "  +---+---+ ",00
 ASC "  | o | o | ",00
 ASC "  | o | o | ",00
 ASC "  | o | o | ",00
 ASC "  | o | o | ",00
 ASC "  | o | o | ",00
 ASC "  | o | o | ",00
 ASC "  '---'---' ",00
 LDA XREG
 SEC
 SBC #'0'
 TAX
 LDA :FIVES,X
 PHA
 LDA :ONES,X
 PHA
 LDA :TENS,X
 PHA
 JMP :PUT
:FIVES DB 12,12,12,12,12,11,11,11,11,11
 DB 12,12,12,12,12,11,11,11,11,11
:ONES DB 14,15,16,17,18,14,15,16,17,18
 DB 14,15,16,17,18,14,15,16,17,18
:TENS DB 14,14,14,14,14,14,14,14,14,14
 DB 15,15,15,15,15,15,15,15,15,15
:PUT PLA
 TAY
 LDX #68
 LDA #" "
 JSR DAT ; tens
 PLA
 TAY
 LDX #72
 LDA #" "
 JSR DAT ; ones
 PLA
 TAY
 LDX #72
 LDA #" "
 JSR DAT ; fives
 RTS

AWPROT LDA WPROT
 EOR #1
 STA WPROT
 LDX #'W'
 JSR SENDST
DWPROT LDX #0
 LDA WPROT
 CMP #'0'
 BNE :ON
 JSR DMESG
 DB 64,4,3
 ASC "(W)  .-.",00
 ASC "     |*|",00
 ASC "     |o|",00
 RTS
:ON JSR DMESG
 DB 64,4,3
 ASC "(W)  .-.",00
 ASC "     | |",00
 ASC "     |*|",00
 RTS

ABAUD INC BAUD
 LDA BAUD
 CMP #'7'
 BNE :SEND
 LDA #'0'
 STA BAUD
:SEND LDX #'B'
 JSR SENDST
DBAUD LDA BAUD
 SEC
 SBC #'0'
 ASL
 ASL
 TAX
 LDA :BTAB,X
 STA :RATE
 LDA :BTAB+1,X
 STA :RATE+1
 LDA :BTAB+2,X
 STA :RATE+2
 LDA :BTAB+3,X
 STA :RATE+3
 LDX #0
 JSR DMESG
 DB 64,21,1
 ASC "(B)aud "
:RATE ASC "xxxx",00
 RTS
:BTAB ASC "100 "
 ASC "300 "
 ASC "600 "
 ASC "1200"
 ASC "2400"
 ASC "4800"
 ASC "9600"

AIRQ INC IRQ
 LDA IRQ
 CMP #'8'
 BNE :SEND
 LDA #'0'
 STA IRQ
:SEND LDX #'I'
 JSR SENDST
DIRQ LDX #0
 LDA IRQ
 ORA #$80
 STA :IRQ
 JSR DMESG
 DB 64,22,1
 ASC "(I)RQ "
:IRQ ASC "x",00
 RTS

AEDITOR INC EDITOR
 LDA EDITOR
 CMP #'3'
 BNE :SEND
 LDA #'0'
 STA EDITOR
:SEND LDX #'E'
 JSR SENDST
DEDITOR LDX EDITOR
 JSR DMESG
 DB 2,8,4
 ASC "(E)ditor",00
 ASC "  vi",00
 ASC "  emacs",00
 ASC "  ed",00
 RTS

ACIPHER INC CIPHER
 LDA CIPHER
 CMP #'3'
 BNE :SEND
 LDA #'0'
 STA CIPHER
:SEND LDX #'C'
 JSR SENDST
DCIPHER LDX CIPHER
 JSR DMESG
 DB 11,8,4
 ASC "(C)ipher",00
 ASC "  des",00
 ASC "  rc5",00
 ASC "  rot13",00
 RTS

ADEFRAG LDA DEFRAG
 CMP #'0'
 BNE DDEFRAG ; only defrag if fragged
 LDX #0
:FINDF LDA DISK,X ; find first free block
 CMP #"."
 BEQ :GOTFREE
 INX
 CPX #30
 BEQ :DONE ; none free -> defragged
 BNE :FINDF ; always taken
:GOTFREE STX COUNT ; save free spot
:FINDX LDA DISK,X
 CMP #"x"
 BEQ :GOTX
 INX
 CPX #30
 BEQ :DONE ; none free -> defragged
 BNE :FINDX
:GOTX LDA #"." ; swap free and alloc'd blocks
 STA DISK,X
 LDX COUNT
 LDA #"x"
 STA DISK,X
 BNE DDEFRAG ; always taken
:DONE LDA #'1'
 STA DEFRAG
 LDX #'D'
 JSR SENDST
 LDA #30
 STA TDEFRAG
 JSR BELL
DDEFRAG LDX #0
 JSR DMESG
 DB 27,19,4
 ASC "(D)efrag",00
DISK ASC "|x...x..x.x..x|",00
 ASC "|..xx.xxx.....|",00
 ASC "               ",00
 LDA DEFRAG
 CMP #'1'
 BNE :RET
 JSR DMESG
 DB 27,22,1
 ASC " 26 SECTORS OK",00
:RET RTS

AJAVA LDA #'1'
 STA JAVA
 LDX #'J'
 JSR SENDST
 LDA #30
 STA TJAVA
DJAVA LDX #0
 JSR DMESG
 DB 41,8,1
 ASC "(J)ava",00
 LDX #0
 LDA JAVA
 CMP #'0'
 BEQ :OFF
 CMP #'1'
 BEQ :ON1
 CMP #'2'
 BEQ :ON2
 CMP #'3'
 BEQ :ON3
 JSR DMESG
 DB 42,9,1
 ASC ".....success",00
 RTS
:OFF JSR DMESG
 DB 42,9,1
 ASC "            ",00
 RTS
:ON1 JSR DMESG
 DB 42,9,1
 ASC "loading jars",00
 RTS
:ON2 JSR DMESG
 DB 42,9,1
 ASC "optimizing  ",00
 RTS
:ON3 JSR DMESG
 DB 42,9,1
 ASC "gc pause    ",00
 RTS

AMODEM STA KEY ; save keypress
 LDA MODEM
 CMP #'0'
 BNE DMODEM ; only enter # if off hook
 LDX #0
:DIG LDA PHONENUM,X
 CMP #" "
 BEQ :GOTBL
 INX
 BNE :DIG ; always taken
:GOTBL LDA KEY
 STA PHONENUM,X
 CPX #6
 BNE DMODEM ; at end of number?
 JSR PHBOOK ; lookup number in directory
 STA MODEM
 LDX #'M'
 JSR SENDST
 LDA #30
 STA TMODEM
 LDA #5
 STA COUNT
:RING JSR BELL
 DEC COUNT
 BNE :RING
DMODEM LDX #0
 JSR DMESG
 DB 42,11,11
 ASC "Modem              ",00
 ASC " .____________.  @,",00
 ASC "/    ______    \@  ",00
 ASC "\____/    \____/   ",00
 ASC "\----/____\----/__.",00
 ASC "'--------------'   ",00
 ASC "   (1) (2) (3)     ",00
 ASC "   (4) (5) (6)     ",00
 ASC "   (7) (8) (9)     ",00
 ASC "   L*I (0) L#I     ",00
 ASC "  [ATDT"
PHONENUM ASC "       "
 ASC "]    ",00
 LDA MODEM
 CMP #'0'
 BNE :ON
 RTS
:ON JSR DMESG
 DB 42,12,4
 ASC "                   ",00
 ASC " .____________.  @,",00
 ASC "/    ______    \@  ",00
 ASC "\____/____\____/   ",00
 RTS
PHDIR ASC "5551212"
 ASC "8675309"
 ASC "6492568"
 ASC "7779311"
 HEX 00
PHBOOK LDA #'1'
 STA KEY ; id of directory num
 LDX #0 ; digit of directory
:NEXTNUM LDA #0
 STA COUNT ; count matching digits
 LDY #0 ; digit of entered num
:TEST LDA PHDIR,X
 BEQ :NONE ; 00 means at end of dir
 CMP PHONENUM,Y
 BNE :NEXTDIG
 INC COUNT ; digit matched
:NEXTDIG INX
 INY
 CPY #7
 BNE :TEST ; check 7 digits of number
 LDA COUNT
 CMP #7
 BEQ :MATCH ; all 7 digits matched
 INC KEY
 BNE :NEXTNUM ; always taken
:MATCH LDA KEY
 RTS
:NONE LDA #'?'
 RTS

SNGTOP EQU 9 ; top of snake grid
SNGLEFT EQU 22 ; left of snake grid
SNMAX EQU 20 ; max fruits or snake len

SNFRUIT DS SNMAX ; where fruit is at
SNFLEN DB 0 ; num fruits
TFRUIT DB 0 ; timer for fruit spawn
SNPOS DB $37 ; snake positions
 DS SNMAX-1
SNLEN DB 1 ; num snake posns
TSNMOVE DB 0 ; timer for snake move
SNDIR DB 0 ; cur snake dir

* set snake direction to A if allowed
* avoid 180 degree turns onto self
SNSDIR LDX SNDIR
 STA SNDIR ; try setting dir
 TXA
 PHA  ; save old dir
 LDA SNLEN
 CMP #2
 BCC :OK
 JSR NPOS ; get next pos
 CMP SNPOS+1 ; is it prev pos?
 BNE :OK
:UNDO PLA
 STA SNDIR ; don't turn on self
 RTS
:OK PLA
 RTS
SNLEFT LDA #3
 JMP SNSDIR
SNRIGHT LDA #1
 JMP SNSDIR
SNUP LDA #2
 JMP SNSDIR
SNDOWN LDA #0
 JMP SNSDIR

* unpack packed position a into x,y
UNPOS PHA  ; save packed pos
 AND #$F
 CLC
 ADC #SNGLEFT
 TAX  ; get x coord
 PLA  ; restore packed pos
 LSR
 LSR
 LSR
 LSR
 CLC
 ADC #SNGTOP
 TAY  ; get y coord
 RTS

* get next packed snake position in A
NPOS LDA SNPOS ; head of snake
 JSR UNPOS
 LDA SNDIR ; decode direction
 CMP #0
 BEQ :DOWN
 CMP #1
 BEQ :RIGHT
 CMP #2
 BEQ :UP
:LEFT DEX
 JMP :PACK
:DOWN INY
 JMP :PACK
:RIGHT INX
 JMP :PACK
:UP DEY
:PACK TYA  ; pack and wrap x,y
 SEC
 SBC #SNGTOP
 AND #7
 ASL
 ASL
 ASL
 ASL
 STA ROW
 TXA
 SEC
 SBC #SNGLEFT
 AND #$F
 ORA ROW ; A has packed pos
 RTS

* redraw everything in snake game
DSNAKE LDA #"."
 LDY #SNGTOP
:NROW LDX #SNGLEFT
:DROW JSR DAT
 INX
 CPX #SNGLEFT+16
 BNE :DROW
 INY
 CPY #SNGTOP+8
 BNE :NROW
 LDA #<SNFRUIT ; draw fruit list
 STA PTR
 LDA #>SNFRUIT
 STA PTR+1
 LDA SNFLEN
 STA COUNT
 LDA #"&"
 JSR :DLIST
 LDA #<SNPOS ; draw snake list
 STA PTR
 LDA #>SNPOS
 STA PTR+1
 LDA SNLEN
 STA COUNT
 LDA #"*"
:DLIST DEC COUNT ; draw char A at list of posns
 BMI :OUT
 PHA  ; save char to draw
 LDY COUNT
 LDA (PTR),Y ; get pos
 JSR UNPOS
 PLA  ; restore char to draw
 JSR DAT
 BNE :DLIST ; always taken
:OUT RTS

* shift snake one position and put A at head
SHSNAKE PHA  ; save new posn
 LDX #SNMAX-1 ; X=to
 LDY #SNMAX-2 ; Y=from
:SH LDA SNPOS,Y ; from 18
 STA SNPOS,X ; ... to 19
 DEY
 DEX  ; stop when to is 0
 BNE :SH
 PLA
 STA SNPOS ; store new head
 RTS

* update snake game
DOSNAKE INC TSNMOVE ; move snake if time
 LDA TSNMOVE
 CMP #4
 BEQ :MOVE
 JMP DOFRUIT ; spawn fruit
:MOVE LDA #0
 STA TSNMOVE
 JSR NPOS ; next snake pos
 JSR INSNAKE
 BNE :CHKGROW
 JMP SNCRASH ; crash!
:CHKGROW LDY SNLEN
 CPY #SNMAX
 BEQ :ETAIL ; can't grow beyond max len
 JSR INFRUIT ; x is fruit index
 BEQ :GROW
:ETAIL PHA  ; save new head pos
 DEY
 LDA SNPOS,Y
 JSR UNPOS
 LDA #"." ; erase tail
 JSR DAT
 PLA  ; restore new head pos
:SHIFT JSR SHSNAKE
 JSR UNPOS
 LDA #"*" ; draw head
 JSR DAT
 JMP DOFRUIT
:GROW PHA  ; save new pos
 LDY SNFLEN ; remove this fruit
 DEY
 LDA SNFRUIT,Y
 STA SNFRUIT,X
 DEC SNFLEN ; consume fruit
 INC SNLEN
 PLA  ; restore new pos
 JMP :SHIFT

* crash snake
SNCRASH LDA #0
 STA SNFLEN
 LDA #1
 STA SNLEN
 LDA #$37
 STA SNPOS
 JSR DSNAKE
 LDA #'1' ; flag snake crashed
 STA SNAKE
 LDX #'N'
 JSR SENDST
 LDA #10
 STA TSNAKE
 RTS

* update fruit for snake game
DOFRUIT INC TFRUIT ; spawn fruit if time
 LDA TFRUIT
 CMP #50
 BNE :OUT
 LDA #0
 STA TFRUIT
 LDA SNFLEN
 CMP #SNMAX ; at max fruit?
 BEQ :OUT ; if so, no more fruit
 JSR RANDOM
 LDA RNG ; random fruit pos
 AND #$7F ; x0..15 y0..7
 JSR INFRUIT
 BEQ :OUT
 JSR INSNAKE
 BEQ :OUT
 LDX SNFLEN ; spawn fruit
 STA SNFRUIT,X ; save fruit pos
 INC SNFLEN ; count new fruit
 JSR UNPOS ; unpack pos
 LDA #"&"
 JSR DAT ; draw fruit
:OUT RTS

* set Z iff packed position A is in fruit
INFRUIT LDX SNFLEN
:NEXT DEX
 BMI :OUT
 CMP SNFRUIT,X
 BEQ :OUT
 BNE :NEXT ; always taken
:OUT RTS

* set Z iff packed position A is in snake
INSNAKE LDX SNLEN
:NEXT DEX
 BMI :OUT
 CMP SNPOS,X
 BEQ :OUT
 BNE :NEXT ; always taken
:OUT RTS

* timers to reset momentary controls
TSNAKE DB 0
TSECGRID DB 0
TMODEM DB 0
TGIBSON DB 0
TDEFRAG DB 0
TJAVA DB 0

* update timers
TIMERS LDA TSNAKE
 BEQ :SECGRID
 DEC TSNAKE
 BNE :SECGRID
 LDA #'0'
 STA SNAKE
 LDX #'N'
 JSR SENDST
:SECGRID LDA TSECGRID
 BEQ :MODEM
 DEC TSECGRID
 BNE :MODEM
 LDA #'0'
 STA SECGRID
 LDX #'S'
 JSR SENDST
 JSR DSECGRID
:MODEM LDA TMODEM
 BEQ :GIBSON
 DEC TMODEM
 BNE :GIBSON
 LDA #'0'
 STA MODEM
 LDX #'M'
 JSR SENDST
 LDA #" "
 STA PHONENUM
 STA PHONENUM+1
 STA PHONENUM+2
 STA PHONENUM+3
 STA PHONENUM+4
 STA PHONENUM+5
 STA PHONENUM+6
 JSR DMODEM
:GIBSON LDA TGIBSON
 BEQ :DEFRAG
 DEC TGIBSON
 BNE :DEFRAG
 LDA #'0'
 STA GIBSON
 LDX #'G'
 JSR SENDST
 JSR DGIBSON
:DEFRAG LDA TDEFRAG
 BEQ :JAVA
 DEC TDEFRAG
 BNE :JAVA
 LDX #1 ; skip | border
:REFRAG LDA DISK,X ; check for valid sector
 CMP #"."
 BEQ :SECTOR
 CMP #"x"
 BEQ :SECTOR
 BNE :NEXT ; always taken
:SECTOR JSR RANDOM
 BIT RNG
 BPL :EMPTY
 LDA #"x"
 BNE :PUT ; always taken
:EMPTY LDA #"."
:PUT STA DISK,X
:NEXT INX
 CPX #30
 BNE :REFRAG ; while sectors remain
 LDA #'0'
 STA DEFRAG ; reset disk state
 LDX #'D'
 JSR SENDST
 JSR DDEFRAG
:JAVA LDA TJAVA
 BEQ :OUT
 DEC TJAVA
 BNE :OUT
 LDA JAVA
 CMP #'0' ; do not advance from 0
 BEQ :OUT
 INC JAVA ; advance to next wait state
 LDA JAVA
 CMP #'5'
 BNE :SENDJ
 LDA #'0' ; wrap back to off state
 STA JAVA
:SENDJ LDX #'J'
 JSR SENDST
 LDA #30 ; re-arm timer
 STA TJAVA
 JSR DJAVA
:OUT RTS

* 8-bit linear feedback shift register
RNG DB $0
* update shift register
RANDOM LDA RNG
 BEQ :EOR
 ASL
 BEQ :NOEOR
 BCC :NOEOR
:EOR EOR #$1D
:NOEOR STA RNG
 RTS

NETIDLE DB 0
IDLE DW 0
SCRVX DB 1
SCRVY DB 1

* screensaver loop
SCRSAVER JSR HOME
:LOOP BIT KBD
 BPL :IDLE
 JMP :OUT
:IDLE JSR RECV ; discard network msgs
 BNE :IDLE ; (all of them)
 LDX #0
 JSR DMESG
:SXL DB 20
:SYL DB 10
 DB 1
 ASC "                      ",00
 LDX #0
 JSR DMESG
:SX DB 20
:SY DB 10
 DB 1
 ASC "Press any key to play.",00
 LDA :SX
 STA :SXL
 LDA :SY
 STA :SYL
 LDA :SX
 CLC
 ADC SCRVX
 CMP #1
 BCC :NEGX
 CMP #55
 BCS :NEGX
 STA :SX
 LDA :SY
 CLC
 ADC SCRVY
 CMP #1
 BCC :NEGY
 CMP #22
 BCS :NEGY
 STA :SY
 JMP :NEXT
:NEGX LDA SCRVX
 EOR #$FF
 STA SCRVX
 INC SCRVX
 BNE :NEXT
:NEGY LDA SCRVY
 EOR #$FF
 STA SCRVY
 INC SCRVY
:NEXT LDA #194
 JSR WAIT
 JMP :LOOP
:OUT LDA KBD ; discard keypress
 BIT KBDSTRB
 LDA #0
 STA IDLE
 STA IDLE+1
 STA NETIDLE
 JSR DINIT
 RTS

* dispatch parsed server command if any
DOCMD LDA CMD
 CMP #CSTAT
 BNE :T2
 JMP :DOSTAT
:T2 CMP #CDISP
 BNE :T3
 JMP :DODISP
:T3 CMP #CPROG
 BNE :T4
 JMP :DOPROG
:T4 CMP #CHULL
 BNE :OUT
 JMP :DOHULL
:OUT RTS
:DOSTAT LDA #<STATUS
 STA PTR
 LDA #>STATUS
 STA PTR+1
 JSR CPLIN
 JMP DSTATUS
:DODISP LDA #<DISP
 STA PTR
 LDA #>DISP
 STA PTR+1
 JSR CPLIN
 JMP DDISP
:DOPROG JSR ATOI
 STA PROGRESS
 JMP DPROG
:DOHULL JSR ATOI
 STA HULL
 JMP DHULL

* main game loop
MAIN
* handle network messages
 INC NETIDLE
:NET JSR RECV
 BEQ :WDOG ; done w messages
 LDA #0
 STA NETIDLE
 JSR DOCMD
 JSR RPARSE ; reset cmd
 JMP :NET
* network watchdog
:WDOG LDA NETIDLE
 CMP #100
 BCC :KEY
 LDA #0
 STA NETIDLE
 JMP RESET
* poll for keypresses
:KEY BIT KBD
 BPL :IDLE
* dispatch key to activate cotrol
 LDA KBD
 BIT KBDSTRB
 JSR ACTIVATE
 LDA #0
 STA IDLE
 STA IDLE+1
 BEQ :TIMERS ; always taken
* update screensaver timeout
:IDLE INC IDLE
 BNE :CHKSCR
 INC IDLE+1
:CHKSCR LDA IDLE+1
 CMP #24
 BCC :TIMERS
 JSR SCRSAVER
:TIMERS JSR TIMERS
 JSR DOSNAKE
* sleep for a while
 LDA #194
 JSR WAIT ; 0.1 sec
 JMP MAIN
