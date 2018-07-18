* slot 3 locations for W5100
WMODE EQU $C0B4
WADDRH EQU $C0B5
WADDRL EQU $C0B6
WDATA EQU $C0B7

* W5100 registers
GW EQU $0001
IRQMR EQU $0016
RMSR EQU $001A
TMSR EQU $001B
* ... socket 0
S0MR EQU $0400 ; mode register
S0CR EQU $0401 ; command register
S0IR EQU $0402 ; interrupt register
S0SR EQU $0403 ; status register
S0LPORT EQU $0404 ; local port
S0FADDR EQU $040C ; foreign address
S0FPORT EQU $0410 ; foreign port
S0TXFSR EQU $0420
S0TXWR EQU $0424
S0RXRSR EQU $0426
S0RXRD EQU $0428
* ... socket 0 params
RXBASE EQU $6000
RXMASK EQU $1FFF
RXMAX EQU RXBASE+RXMASK+1
TXBASE EQU $4000
TXMASK EQU $1FFF
TXMAX EQU TXBASE+TXMASK+1
* socket commands
SCOPEN EQU $01
SCCONN EQU $04
SCCLOSE EQU $10
SCSEND EQU $20
SCRECV EQU $40
* socket status
STCLOSED EQU $00
STINIT EQU $13
STEST EQU $17

* for standalone assembly testing
 DO 0
 ORG $6000
RESET JSR NINIT
 BEQ :OK
 JMP NETERR
:OK JSR SENDAN
:MAIN JSR RPARSE
 JSR RECV
 LDA CMD
 BEQ :MAIN
 CMP #2
 BNE :T2
 LDA #'S'
 JSR COUT
 JSR :ASTR
 JMP :MAIN
:T2 CMP #1
 BNE :T3
 LDA #'D'
 JSR COUT
 JSR :ASTR
 JMP :MAIN
:T3 CMP #3
 BNE :T4
 LDA #'P'
 JSR COUT
 JSR :AINT
 JMP :MAIN
:T4 CMP #4
 BNE :T5
 LDA #'H'
 JSR COUT
 JSR :AINT
:T5 JMP :MAIN
:AINT JSR ATOI
 JSR PRBYTE
 LDA #$8D
 JSR COUT
 RTS
:ASTR LDY #0
 LDA #'"'
 JSR COUT
:PR CPY ASTRL
 BEQ :ASTRO
 LDA ASTR,Y
 JSR COUT
 INY
 JMP :PR
:ASTRO LDA #'"'
 JSR COUT
 LDA #$8D
 JSR COUT
 RTS

PTR EQU $06
COUNT EQU $08
SLOG EQU $0A
SPHY EQU $0C
COUT EQU $FDED
PRBYTE EQU $FDDA

NETERR BRK
 USE W5100
 PUT CONFIG
 FIN

* initialize uthernet and connect to server
* Z set on success.
NINIT LDA #$80 ; reset W5100
 STA WMODE
:RST LDA WMODE ; wait for reset
 BMI :RST
 ORA #3
 STA WMODE ; address auto-inc
 JSR IFCONFIG
 JMP S0CONFIG

* set common W5100 config
IFCONFIG WAI GW ; start of config block
 LDX #0
 BEQ CPCONF ; always taken
NETCONF
GATEWAY DB 10,110,0,48
NETMASK DB 255,0,0,0
MAC DB $00,$08,$DC,$01,$02,$03
IPADDR DB 10,110,0,46
NCONFLEN EQU *-NETCONF
CPCONF LDA NETCONF,X ; store config
 STA WDATA
 INX
 CPX #NCONFLEN
 BNE CPCONF
 WSI RMSR;#3 ; 8KB for socket 0 rcv
 WSI TMSR;#3 ; ditto xmit
 WSI IRQMR;#0 ; disable interrupts
 RTS

* configure socket 0. Z set on success.
S0CONFIG WSI S0MR;#$21 ; tcp, no delayed ack
 WSI16 S0LPORT;$C000 ; local port
 WAI S0FADDR ; foreign ip
 LDA #10
 STA WDATA
 LDA #110
 STA WDATA
 LDA #0
 STA WDATA
 LDA #1
 STA WDATA
 WSI16 S0FPORT;8000 ; foreign port
 WSI S0IR;#0 ; ack any pending irq
 WSI S0CR;#SCCLOSE ; in case left open
 WSI S0CR;#SCOPEN ; open it
 WLI S0SR
 CMP #STINIT
 BNE :OUT ; error if not INIT state
 WSI S0CR;#SCCONN ; connect
:WAIT WLI S0SR
 BNE :TEST
 LDA #1 ; 0 = SOCK_CLOSED, error
 RTS
:TEST CMP #STEST ; connection established?
 BNE :WAIT
:OUT RTS

* send message PTR, size COUNT. Z set on success.
SEND WLI S0CR
 BNE SEND ; wait for prev cmd
 WAI S0TXFSR ; check tx buf space
 LDX WDATA ; high byte of buf space
 LDA WDATA ; low byte of buf space
 SEC  ; compute X:A-COUNT
 SBC COUNT
 PHA
 TXA
 SBC COUNT+1
 TAX  ; subtract 4 bytes for len
 PLA
 SEC
 SBC #4
 TXA
 SBC #0
 BPL :ADDR
:NOROOM LDA #1 ; not enough send buf
 RTS
:ADDR WLI S0TXWR ; compute base address
 STA SPHY+1
 AND #>TXMASK
 ORA #>TXBASE
 STA SLOG+1
 LDA WDATA ; low byte of phys addr
 STA SPHY
 AND #<TXMASK
 ORA #<TXBASE
 STA SLOG
 LDA #0 ; copy len. byte 3 is 0.
 JSR :WBYTE
 LDA #0 ; byte 2 is 0
 JSR :WBYTE
 LDA COUNT+1 ; byte 1 is high count
 JSR :WBYTE
 LDA COUNT ; byte 0 is low count
 JSR :WBYTE
:COPY LDA COUNT ; copy message
 ORA COUNT+1
 BEQ :DONE ; no bytes left to copy
 LDY #0
 LDA (PTR),Y
 JSR :WBYTE ; write it
 LDA COUNT ; count it copied
 BNE :DC
 DEC COUNT+1
:DC DEC COUNT
 INC PTR ; advance message ptr
 BNE :COPY
 INC PTR+1
 BNE :COPY ; always taken
:DONE WAI S0TXWR ; update tx buf ptr
 LDA SPHY+1
 STA WDATA
 LDA SPHY
 STA WDATA
 WSI S0CR;#SCSEND ; issue SEND command
 LDA #0 ; success, set Z flag
 RTS
* write one byte to socket buffer
:WBYTE WAR SLOG ; address cur buf byte
 STA WDATA ; save data there
 WINC TXBASE;TXMAX ; increment and wrap
 RTS

* receive message json and parse it
* return 1 if message received, 0 if not.
RECV WLI S0CR
 BNE RECV ; wait for prev cmd
 WAI S0RXRSR ; get read size
 LDA WDATA
 STA COUNT+1
 LDA WDATA
 STA COUNT
 CMP #4 ; need at least 4 byte len
 BCS :GOTLEN
 LDA COUNT+1
 BNE :GOTLEN
:NOPE LDA #0
 RTS
:GOTLEN WAI S0RXRD ; compute sock buf addr
 LDA WDATA
 STA SPHY+1
 AND #>RXMASK
 ORA #>RXBASE
 STA SLOG+1
 LDA WDATA
 STA SPHY
 AND #<RXMASK
 ORA #<RXBASE
 STA SLOG
 JSR :RBYTE ; read message length
 JSR :RBYTE ; (assume bytes 3,2 are 0)
 JSR :RBYTE
 STA PTR+1 ; len in PTR
 JSR :RBYTE
 STA PTR
 LDA COUNT+1 ; do we have it all?
 CMP PTR+1
 BCC :NOPE
 BNE :READ
 LDA COUNT
 CMP PTR
 BCC :NOPE
:READ LDA PTR+1 ; read just this message
 STA COUNT+1
 LDA PTR
 STA COUNT
 JSR RPARSE ; reset parse state
:RL LDA COUNT
 ORA COUNT+1
 BEQ :DONE
 JSR :RBYTE ; read next msg byte
 JSR PARSE ; update parser
 JMP :RL
:DONE WAI S0RXRD ; advance read ptr
 LDA SPHY+1
 STA WDATA
 LDA SPHY
 STA WDATA
 WSI S0CR;#SCRECV ; issue RECV command
 LDA #1
 RTS
* read one byte from socket buf
:RBYTE WAR SLOG ; address byte to read
 LDA WDATA ; read the byte
 PHA  ; save read byte
 WINC RXBASE;RXMAX ; inc buf ptr
 LDA COUNT ; count byte read
 BNE :DC
 DEC COUNT+1
:DC DEC COUNT
 PLA  ; return byte read
 RTS

* commands
CUNK EQU 0
CDISP EQU 1
CSTAT EQU 2
CPROG EQU 3
CHULL EQU 4

* parser state
CMD DB CUNK ; seen cmd if any
CHAR DB 0 ; current char
NEST DB 0 ; {} nesting depth
INSTR DB 0 ; inside "" string
COLON DB 0 ; after : at cur depth
ASTRM EQU 78 ; max arg bytes
ASTR DS ASTRM ; argument
ASTRL DB 0 ; arg str len
CSTRM EQU 15 ; max command bytes
CSTR DS CSTRM ; command
CSTRL DB 0 ; command str len

* reset parser state
RPARSE LDA #0
 STA CMD
 STA CHAR
 STA NEST
 STA INSTR
 STA COLON
 STA CSTRL
 STA ASTRL
 RTS

* hackily parse one byte of message json
* cmd is any matching string at nesting depth 1
* arg is whatever follows : at depth 2, w/o quotes
PARSE STA CHAR ; save input char
 CMP #':'
 BNE :T1
 LDA #1 ; flag ':' seen
 STA COLON
 RTS
:T1 CMP #'{'
 BNE :T2
 INC NEST ; inc nesting depth
 LDA #0 ; reset ':' flag
 STA COLON
 RTS
:T2 CMP #'}'
 BNE :T3
 DEC NEST ; dec nesting depth
 LDA #0
 STA COLON
 RTS
:T3 CMP #'"'
 BNE :T4
 LDA NEST
 CMP #1 ; skip "s in args
 BNE :IGN
 LDA INSTR
 BEQ :STARTS
:ENDS JSR FINDCMD
 LDA #0
 STA INSTR
:IGN RTS
:STARTS LDA #0 ; start ": reset len
 STA CSTRL
 LDA #1
 STA INSTR
 RTS
:T4 LDA NEST
 CMP #1
 BEQ :CMD ; depth 1 is commands
 CMP #2
 BEQ :ARG ; depth 2 is args
 RTS
:CMD LDA INSTR
 BEQ :CMDO ; outside string?
 LDA CSTRL
 CMP #CSTRM ; at max len?
 BEQ :CMDO
 LDA CHAR ; save char of string
 LDX CSTRL
 STA CSTR,X
 INC CSTRL
:CMDO RTS
:ARG LDA COLON
 BEQ :ARGO ; before colon?
 LDA ASTRL
 CMP #ASTRM ; at max len?
 BEQ :ARGO
 LDA CHAR ; save char of arg
 LDX ASTRL
 STA ASTR,X
 INC ASTRL
:ARGO RTS

* set CMD if CSTR matches any known commads
FINDCMD LDX #0
:CHECKS LDA :CMDS,X
 STA PTR
 LDA :CMDS+1,X
 STA PTR+1
 TXA
 PHA  ; save cmd table index
 LDY #0
 LDX #0
:CMPS CPX CSTRL
 BEQ :ENDC
 LDA (PTR),Y
 BEQ :NOMATCH ; end of table entry/not cmd
 CMP CSTR,X
 BNE :NOMATCH ; char mismatch
 INX
 INY
 BNE :CMPS ; always taken
:ENDC LDA (PTR),Y
 BEQ :MATCH ; end of cmd + table entry
:NOMATCH PLA
 TAX
 INX
 INX
 CPX #8
 BNE :CHECKS
 BEQ :DONE ; always taken
:MATCH PLA
 INY  ; skip nul
 LDA (PTR),Y
 STA CMD
:DONE RTS
:SETD ASC 'set-display',00
 DB CDISP
:SETS ASC 'set-status',00
 DB CSTAT
:SETP ASC 'set-progress',00
 DB CPROG
:SETI ASC 'set-integrity',00
 DB CHULL
:CMDS DA :SETD
 DA :SETS
 DA :SETP
 DA :SETI

* convert arg for cmd to an integer 0-100.
ATOI LDA ASTRL
 CMP #0
 BNE :T1
 RTS ; if missing, assume 0
:T1 CMP #1
 BNE :T2
 LDA ASTR ; one digit
 SBC #'0' ; (carry=1 from CMP)
 RTS
:T2 CMP #2
 BNE :MAX
 LDA #0 ; two digits
 LDX ASTR
:MUL10 CPX #'0'
 BEQ :ONES
 DEX
 CLC
 ADC #10
 BNE :MUL10 ; always taken
:ONES CLC
 ADC ASTR+1 ; add ones digit
 SEC
 SBC #'0'
 RTS
:MAX LDA #100 ; cap at 100
 RTS

* copy string arg to PTR, fill with " ".
* used for DISPLAY or STATUS.
CPLIN LDY #0
:CP CPY ASTRL
 BEQ :FILL
 LDA ASTR,Y
 STA (PTR),Y
 INY
 BNE :CP ; always taken
:FILL CPY #78
 BEQ :OUT
 LDA #' '
 STA (PTR),Y
 INY
 BNE :FILL ; always taken
:OUT RTS

* set-state message template
STATE ASC '{"message":"set-state","data":{"id":"'
STATEID ASC 'x","state":"'
STATEST ASC 'x"}}'
STLEN EQU *-STATE

* send set-state message on control update
* X is the control and A is the new state
SENDST STX STATEID
 STA STATEST
 LDA #<STATE
 STA PTR
 LDA #>STATE
 STA PTR+1
 LDA #<STLEN
 STA COUNT
 LDA #>STLEN
 STA COUNT+1
 JSR SEND
 BEQ :OUT
 JMP NETERR
:OUT RTS

* send initial announce message
SENDAN LDA #<ANNOUNCE
 STA PTR
 LDA #>ANNOUNCE
 STA PTR+1
 LDA #<ANLEN
 STA COUNT
 LDA #>ANLEN
 STA COUNT+1
 JSR SEND
 BEQ :OUT
 JMP NETERR
:OUT RTS
