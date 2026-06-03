### Idea

Design protocol to let Main MCU communicate with help of a single pin.

Constraints on Main MCU side, we will call it Transmitter:
  - Single pin only:
     - Z state (can read line)
     - Pull Up (can read line)
     - Active High (cannot read line)
     - Active Low (cannot read line)
  - No need of special dedicated hardware (like SPI/UART/I2C)
  - Timing constraints:
    - Transmitter is allowed to act without any time constraints
      - from Receiver perspective:
        - Transmitter cannot react on time based constraints
        - any time can pas before Transmitter registers line change and acts
    - Transmitter library code is minimized to spend as little time as possible to handle line
    - Transmitter has no need to keep track of time
    - Transmitter should always give Receiver minimum 4uS to act
    
Constraints on Receiver side:
  - Timing constraints:
    - Receiver on contrary must dedicate all resources to monitor and instantly act.
    - Receiver can pull line down (via 300 Ohm resistor). Or pull line up via same resistor.
  - It should use any hardware possible
  - It's main goal is to responde to Transmitter as soon as possible
  - It must act within 1.5uS

--------

### My Hand-Made protocol

```
Possible Line states

 RX\TX:| Z | U | D |
 ------+------------
   Z   | Z | H | L |
   U   | H | H | L |
   H   | H | H | H |
   L   | L | L | L |

--------------------------------------------------------
Scenario A: RX Starts After TX. Successfull handshake.

/// Testing: no M state
RXST: 0       1   2 3      4      5 6     7      8 9   a b    c d    
  RX:             r r......r......r r.....r r....r r...r r....r r....
  RX: Z       U    D               U       D      U     D      U
  LN: Z...H........L......H......L.......H......L.....H......L.H.....
  TX: Z   U               H      L       H      L     H      U      
  TX:           r   r    r                                          r
TXST: 0   1     2   3    4       5       6      7     8      9      a


/// Testing: Reading 0 (HU)
// RXST=7 - RX Replies with 0
RXST:     1       2 3      4      5 67     
  RX:             r r......r......r r......r r...r r
  RX:     U        D               U D      D     U
  LN: ....H........L......H......L.H.L....H.....L.H.
  TX:     U               H      U        H     U
  TX:           r   r    r               r      
TXST:     1     2   3    4       5       6      7
// TXST=6 - TX Reads 0
// FIXME: Apply protocol change (starting packet HU=>HLHU)

/// Testing: Reading 1 (HLU)
// RXST=7 - RX Replies with 1
RXST:     1       2 3      4      5 67     8     9 a b    ...    23
  RX:             r r......r......r r......r.....r r r            r..........r
  RX:     U        D               U              D U            D            
  LN: ....H........L......H......L.H......L.....H.L.H............L..........H
  TX:     U               H      U        L     U                           H
  TX:           r   r    r               r              r ...  r     r     r
TXST:     1     2   3    4       5       6      7       8 ...  2     3     4
// TXST=6 - TX Reads 1
// FIXME: Apply protocol change (starting packet HU=>HLHU)


/// Start And Handshake (TX is powered first)
RXST: 0       1   2 3      4      5 6     7      8 9   a b    c de    
  RX:             r r......r......r r.....r r....r r...r r....r r ....
  RX: Z       U    D               U       D      U     D      U D    
  LN: Z...H........L......H......L.......H......L.....H......L.H.L....
  TX: Z   U               H      L       H      L     H      U        
  TX:           r   r    r      r       r      r     r      r        r
TXST: 0   1     2   3    4       5       6      7     8      9       a
   S:                    1      2       3      4     5      6        7

Legend (LN):
  Z,L,H - line is stable in any of this state
Legend (RX/TX):
  './ /-' - same state as before
  r - read line
  U/D - Pull Up/Down
  H/L - Actively drive High/Low
  Z - High impedance state
  
  HLHLHU  - handshake

  HU      - write 0
  HLHU    - write 1
  LU      - confirms end of transmission

  HLHU    - requests read
  HU      - confirms 0
  LU      - confirms 1
  

RXST - RX State
  1 - powered
  2 - Ready, reads line:
      If line == L : TX is pulling line down, either we are in the middle of incomplete communication frame
                        or something is wrong => Error => 1s sleep => Go to step 1
      If line == H : TX Is not in fail mode 
                      => Attempt communicate with TX 
                      => changes output to Pull Down
  3 - Reads line just after Pull Down:
      If line == H within 2uS - TX Is driving it Up => Error => 1s sleep => Go to step 1
      If line == L within 2uS - go to step 4
  4 - Keeps monitoring for line to go Low
        If timeout => goto step 1
        If line == H : TX is ready 
                       => RX Pulls line Up and waits for TX to drive (ownership transfered)
  5 - ... waits for Low
  
  e - enables pull-down to signal TX that RX is ready to continue
        FIXME In future RX would be able to PullUp to signal TX that it should wait for line become Low
  
TXST - TX State
  1 - powered
  2,3 - checks if RX Ready (line == H) : Goto step 2
                           (line == L) : continue
    NOTE: At step 3 we can start handshake - but we do not - we let RX see line transitionings and act accordingly
  4 - checks again if RX Ready (line == H) : Goto step 2
                               (line == L) : Starts handshake => Drives Line Actively High
  5 - ...shaking hand => Activates Pull-Up
  6 - ...shaking hand (line == L) : Goto step 1 (we need to restore Pull Up)
                      (line == H) : Continue => Drive line Actively Low
  6 - and one more shake (line == H) : Goto step 2
                         (line == L) : Continue
  7 - changes output to PullUp - so it can read line state
  8 - checks
      If line == H : handshake failed => GoTo step 2
      If line == L : handshake was successful, Continue to Read stage

```



Protocol description

Setup:

  RX and TX both pull line up at initialization stage

Handshake:

  RX Tries to pull line down and monitors if it gets up - meaning TX is present and communicates.
  TX periodically checks if line becomes L. When it does, it waits for next cycle (>= 4uS) and starts handshake sequence:
     `HLHLHU`
  RX verifies after each line change if its' H/L or U by pulling line into opposite direction
        - if it doesn't changes - its H/L
        - if it does change - it's U

Sending data:

  RX Pulls line Down
  TX when notices line is Low again, it sends one of two:
     `HU`  - sends 1
     `HLU` - sends 0
  RX verifies after each line change - same as for handshake
  Note: RX Leaves line in Pull-Up state, before it is ready to continue communication

Receiving data:
  RX Pulls line Down
  TX when notices line is Low, it sends: 
        `HLHU`
  RX After receiving U pulls line into Up (for 1) or Down (for 0)
  TX Reads line and replies with:
        `LU` (to confirm 1) or
        `HU` (to confirm 0)

-----

On higher level protocol:

  - Handshake
  - TX sends 1 system byte describing:
    - message size (5 bits) (1..32)
    - flags for last two communications indicating crc/frame error
  - TX sends N-data bytes
  - TX sends CRC byte (system + data bytes)
  - TX reads 1 control byte from RX
  - TX reads 1 crc byte (accounting control byte) from RX to validate frame


-----

#### Another view on protocol

TX Sends packets. Each packet starts with H and ends with U.

RX must always strongly pull line into opposite direction to the signal.
This is required to be able to detect when TX changes signal from H to U or from L to U.
(To better understand this - imagine line is H, without strongly pulling line down - 
    when TX changes line into U - no line state change occurs (line remains H),
    so RX will not see state change)

When packet ends RX can either:
  - pull line strongly Down - to let TX continue transmission
  - or pull line strongly Up - to either signal TX:
    - that transmission is failed - if in the middle of frame
    - or it is the end of transmission - all bits received
    
Packets are:
  - HLHLHUU - handshake
    HLHLHlL
  
  - HUU  - send 1
    HlL
  - HLUU - send 0
    HLhL
  
  - HUU - end of transmission
    HlH  <- RX wants to rest
    HlL  <- RX ready for next round
  
  - HLHU - request bit from RX
    as a reply - RX either sets line SPU (1) or SPD(0)
    based on that TX sends either LU packet (on response to 1) or HU packed (on response to 0)
    
    
NOTES: When Line is forced Low (due to RX malfunction) - TX will see it as always ready to work.
       We need some mechanism to let TX know that RX malfunctioned.
       We could change handshake to something like: HLHLU LHU

NOTE2: What if TX pin is shorted to GND on RX side? Then it will get burned. We need mechanism to protect it.