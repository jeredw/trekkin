* address a w5100 register immediately
WAI MAC
 LDY #>]1
 STY WADDRH
 LDY #<]1
 STY WADDRL
 <<<
WAR MAC
 LDY ]1+1
 STY WADDRH
 LDY ]1
 STY WADDRL
 <<<
* load 8 bits from w5100 reg
WLI MAC
 WAI ]1
 LDA WDATA
 <<<
* store 8 bits to w5100 reg
WSI MAC
 WAI ]1
 LDY ]2
 STY WDATA
 <<<
* store 16 bits to w5100 reg
WSI16 MAC
 WAI ]1
 LDY #>]2
 STY WDATA
 LDY #<]2
 STY WDATA
 <<<
* increment logical and physical sock ptrs and wrap
WINC MAC
 INC SLOG
 BNE ]P
 INC SLOG+1
]P INC SPHY
 BNE ]W
 INC SPHY+1
]W LDA SLOG
 CMP #<]2
 BNE ]OUT
 LDA SLOG+1
 CMP #>]2
 BNE ]OUT
 LDA #<]1
 STA SLOG
 LDA #>]1
 STA SLOG+1
]OUT
 <<<
