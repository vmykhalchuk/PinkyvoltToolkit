
## Technical details

### Communication protocol

Transmitter and Receiver are connected via single line.

Transmitter utilizes D5 pin (in three state mode) that directly connects to line.

Receiver connects D5 (in input mode) directly to line, D6 to output of comparator that compares line voltage to 0.12V reference.
Additionally Receiver connects D7 (as output) via 2.7k resistor to LINE. When D7 is set to OUTPUT+LOW it pulls Line Down. And is disengaged when D7 is set to INPUT.

#### Starting sequence:
  - Transmitter
    - sets D5 to Input with internal pullup enabled
    - waits for line to become LOW (if HIGH - no Receiver connected, or connection issue)
  - Receiver
    - makes sure D7 is LOW (2k resistor pull-down is disabled)
    - waits for line to become M (if LOW - no Transmitter connected, or connection issue)

#### Handshake:
  - Receiver
    - pulls line down via 2.7k resistor (set D7 to HIGH), waits 1ms
    - waits for line to become M (if it is LOW - no Transmitter connected, or connection issue)

#### Protocol:

**Notes:** Internal or external comparator (LM393 or similar) will monitor line.
         Line length <= 2cm (comparator will be placed near connector and powered from Transmitter, then will send data over longer line to Receiver)


#### Hardware implementation

** Connect Transmitter via 200 Ohm resistor to avoid short circuit **

Select Comparator for task

|   IC    |   time  | offset | Ib  | Umin |
|:-------:|:-------:|:------:|:---:|:----:|
| LM393N  |  1.5uS  |  5mV   | 25nA|      |
| MCP6541 |   4uS   |  7mV   | 1pA |      |
| LM311DR |  115nS  | 7.5mV  |250nA| 3.5V |
| LM311DR |  200nS  | 7.5mV  |250nA|  5V  |
| LM311P  |  115nS  | 7.5mV  |250nA| 3.5V |

**LM393N circuit with hysteresis 0.02V**
  - R1=47k, R2=1k, RH=240k, Rp=10k => 90-110mV
  - R1=33k => 140-160mV
  - Input goes to (-)
  - R1 between VCC and (+)
  - R2 between GND and (+)
  - RH between (+) and Output
  - Rp between Output and VCC


#### Pull-down specification

|     |  2k / 5V  | 2k / 4.5V | 1.9k / 4.5V |
|:---:|:---------:|:---------:|:-----------:|
| 55k | 0.175     | 0.158     | 0.150       |
| 50k | 0.192     |           |             |
| 30k | 0.313     |           |             |
| 20k | 0.455     |           |             |
| 15k | 0.588     |           | 0.506       |

*V=5V*2k/(55k+2k)*

**Alternative (2.7k):**

|     |  2.7k/5V  | 2.7k/4.5V |  2.6k/4.5V  |
|:---:|:---------:|:---------:|:-----------:|
| 55k | 0.234     | 0.211     | 0.196       |
| 50k | 0.256     |           |             |
| 30k | 0.413     |           |             |
| 20k | 0.595     |           |             |
| 15k | 0.763     |           | 0.636       |

*V=5V*2.7k/(55k+2.7k)*

#### Design improvement - Active pull-up at start

##### Idea 1 : Suggestion 1
Currently when we connect Receiver to Transmitter - connection will present jittering on line, causing protocol failures.
To prevent this - we can drive Line on Receiver side to HIGH via 200Ohm resistor.
And after a while - when connection established - disconnect that pull-up.

Cons: Receiver should be commanded by user to start communication.
   This is tradeoff - tolerate communication failure at start and let timeouts restart it, however user doesn't have to do anything
                    - or force user to start communication after physical connection is done
Cons: User might forget to disable communication and break physical connection and re-connect again - causing initial issue we try to solve here.

##### Idea 1 : Suggestion 2 (best if this resistor will not affect Comparator)

We can instead pull LOW via big resistor (400K), and monitor input - when it comes HIGH (due to PU on TX Side - after delay we can start communication)
Also Pull down should be very close to connector and should use comparator to send strong signal to MCU.

##### Idea 1 : Suggestion 3

Add Debouncing stage into TX Handshake FSM (debounce when Handshake starts)


#### Electrical considerations

**Ask AI to solve this:**
We have two atmega328 mcus connected via single line 10cm long (Source D4 to Target D5).
D4 is Input no internal pullup. D5 in input no internal pullup.
At start Target pulls line down via 2.7k resistor (via its' D7 pin).
After a while Source activates internal pull-up and line settles.
Then after another while Source drive line High.
And then after line settles drives line Low.

Describe in details that final transition (how long it takes, why, what voltage transients are)

**Also do it for other transitions**

----

## Communication Protocol

### Physical connection

**TX Device** is a user-device that exposes interface to read its' internal error flags, or any other status flags.
**RX Device** is a monitor that reads these flags and can also send one byte command to **TX Device**.
**RX** and **TX** devices are connected via single line.

**TX Device** connects to line via single bare pin (any of digital pins that supports internal pull-up, and three-states `[Z,L,H]`).
Internal pull-up resistance is expected to be within 25k-45k range ideally.

**TX** can drive line:
  - U - Pull-up via Internal resistance
  - H - Actively drive High (to VCC Rail)
  - L - Actively drive Low (to GND Rail)
  - Z - Switch to high impedance state

**RX Device** connects to line by following methods:
  - via `CMP` pin that reads Comparator's output. Comparator compares input line against set voltage (~0.15V),
     if output of comparator is `0` (it is inverted output) - line state is either `H` or `M` state.
  - via PIN pin that reads line directly to determine if its H or L (in pair with CMP it determines if line is in `H`,`L` or `M` state)
  - via PD pin that connects to line through 2.7k resistor

**RX** can drive line:
  - U - Pull-up through 2.7k resistor to VCC Rail
  - D - Pull-down through same 2.7k resistor to GND Rail
  - Z - Switch to high impedance state

**Line** Can be in one of three states:
  - `H` (VCC)
  - `L` (GND)
  - `M` - voltage formed by **TX** Internal PullUp and by **RX** PullDown via 2.7k resistor
  
**Note:** Only **RX** can read three states of line, **TX** sees `M`-state as L and cannot distinguish `L` from `M` or vice-versa.
  
### Timing

**TX Device**
  - Has no timing limits to respond to signal change. It doesn't have to maintain
    stable line clocking and can sporadically delay next line transition if needed.
    Line transitions are irregular.
  - There are only two constraints:
    - **C1:** **TX** should never change line state more often than every **4uS**
    - **C2:** And should always transition to next logical state in one go:
      For example if line is H, and TX wants to change it to L (TX is in PU, RX is in Z).
      To transition line to L TX has to:
         - disable Pull-up
         - enable Low output
       
    However if it performs this with delay between step 1 and step 2 - Line is left floating for indetermined time.
    Instead **RX** should always change to make line state transition smooth, e.g.: first change from `U` -> `H`. Then switch `H`->`L`.

**RX Device**
 - should react to signal change within 1.5uS window (remaining 2.5uS are left to let signal stabilize)
 - should reject fast-signal transition, for example when line changes from `H` to `L` 
     it briefly goes through `M` state, which should be ignored.

### Handshake

**Scenario A:** **RX** is connected to **TX** that is powered On, then **RX** is powered On. **RX** Is properly calibrated.
```
RX: Z       U   D            -  
TX: Z   U           Z   U   H   U   H   LZ   U
LN: Z   H   H   M   L   M   H   M   H   LL   M

ST: 1   2   3   4   5   6   78  9   A   B    C


1 - All devices Off
2 - TX is On, pulls line Up
3 - RX is On, also pulls line Up
4 - RX is ready, pulls line Down
5 - TX reads L and initializes Handshake
        now RX should see that line transitioned from M to L (*1)
8 - When RX reads H (missing M->L->M transitions) - then it stops Handshake and recalibrates
9 - final confirmation of handshake
B - TX is doing two changes at once, however it doesn't break second contraint since line state doesn't change (it remains L).
     Also at step C it will be able to check if line is L (meaning RX hasn't disconnected)
C - if TX reads line as L - handshake has succeeded, if H - failed - restarts from step 2

NB1: If at any step, except steps: {9 or B} TX reads line as H - handshake failed - restart from step 2

Note *1: If RX Cannot see transition between M and L - it's comparator is not sensitive enough - should be trimmed

Legend:
RX: Z - high impedance state, no pull-up; U - pull up via 2.7k resistor; D - pull down via same 2.7k resistor
TX: Z - high impedance state, no pull-up; U - internal pullup; H - drive line High; L - drive line Low
LN: L - near 0 volts; H - near VCC; M - voltage formed by internal pull-up resistance of TX and 2.7k pull down resistor of RX
```

**Scenario B:** RX is connected to TX that is powered On, then RX is powered On. RX Is NOT properly calibrated (reads all M/L line states either as L or M)
```
RX: Z       U   D            Z
TX: Z   U           Z   U   H   U
LN: Z   H   H   M   L   M   H   H

ST: 1   2   3   4   5   6   78  9

8 - RX reads line as H, from its' perspective it was constantly L (or M depending on type of calibration issue)
      RX makes conclusion that it was unable to differentiate line state M from L, and should recalibrate.
      It disconnects all line pulls (no pull-down no pull-up) to indicate to TX that handshake failed
9 - TX reads line as H, handshake failed, pulls line up - restart from step 2




---------------- TEST SECTION -----------------------
Scenario ?:

RXST: 0       1   2               3    45       6
  RX: Z       U   D               rU   rr       rD
  LN: Z...H...H...M....L....M....H....ML......MH.M.....
  TX: Z   U     r     rZ   rU   rH    L     rZU      r-
TXST: 0   1     2     3    4    5     6      7        8

Legend:
  './ /-' - same state as before
  r - read line
  U/D - Pull Up/Down
  H/L - Actively drive High/Low
  Z - High impedance state

RXST - RX State
  1 - powered
  2 - Ready
  
TXST - TX State
  1 - powered
  2 - checks if RX Ready : No
  3 - checks if RX Ready : Yes - Starts handshake
  4 - ...shaking hand
  5 - and one more shake
  6 - ??? TX reads line as H - it indicates that RX abandoned handshake
  7 - ??? TX signals to RX that it confirms 




---------------- LETS DESCRIBE AGAIN --------------------
--------------------------------------------------------
Scenario A: RX Starts After TX. Successfull handshake.

RXST: 0       1   2 3        4    5    6    7      
  RX: Z       U   rDr        r    r    r    r       
  LN: Z...hH.......mM.......lL...mM...hH...mM......
  TX: Z   U     r      r   rZ   rU   rH    U     r-
TXST: 0   1     2      3   4    5    6     7     8

/// Alternative, when handhsake failed
RXST: 0       1   2 3                  6           
  RX: Z       U   rDr                  rZ           
  LN: Z...hH.......lL.................hH...........
  TX: Z   U     r      r   rZ   rU   rH    U     r
TXST: 0   1     2      3   4    5    6     7     8

/// Testing: no M state
RXST: 0       1   2 3   4   5      6     7    8
  RX: Z       U   rDr   r   rU     rD    r    r
  LN: Z...hH.......lL..hH..lLhH...lL....hH...lL.
  TX: Z   U     r     rH   U     rL     H    U
TXST: 0   1     2     3    4     5      6    7



Legend (LN):
  l,m,h - line transitions from previous state to L,M,H accordingly
  Z,L,M,H - line is stable in any of this state
Legend (RX/TX):
  './ /-' - same state as before
  r - read line
  U/D - Pull Up/Down
  H/L - Actively drive High/Low
  Z - High impedance state

RXST - RX State
  1 - powered
  2 - Ready, reads line:
      If line == L or M : TX is pulling line down, either we are in the middle of incomplete communication frame
                          or something is wrong => Error => 1s sleep => Go to step 1
      If line == H : TX Is not in fail mode, changes output to Pull line Down
  3 - Reads line just after Pull Down:
      If line == H within 2uS - TX Is driving it Up => Error => 1s sleep => Go to step 1
      If line == L within 2uS - TX is not connected yet => Error => 1s sleep => Go to step 1
                                !!! OR RX Is not calibrated and reads line as L instead of M
      If line == M within 2uS - Everything right, continue
  4 - Reads line:
      If it is H => Error => 1s sleep => Go to step 1
      If it is L => Success => continue
  5,6,7 - Just like Step 4, wait for certain line state, if not => Error => 1s sleep => Go to step 1      
  
TXST - TX State
  1 - powered
  2,3 - checks if RX Ready (line == H) : Goto step 2
                           (line == L) : continue
    NOTE: At step 3 we can start handshake - but we do not - we let RX see line transitionings and act accordingly
  4 - checks again if RX Ready (line == H) : Goto step 2
                               (line == L) : Starts handshake
  5 - ...shaking hand (line == H) : Goto step 1 (we need to restore Pull Up)
                      (line == L) : Continue
  6 - and one more shake (line == H) : Goto step 2
                         (line == L) : Continue
  7 - changes output to PullUp - so it can read line state
  8 - checks
      If line == H : handshake failed => GoTo step 2
      If line == L : handshake was successful, Continue to Read stage

--------------------------------------------------------
Scenario B: RX Starts After TX. RX reads M state as L due to bad calibration.

  


```
