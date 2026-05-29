Is there a need in community of communication protocol for Arduino AVR boards (Nano,Mega,Tiny) that 
lets user send modest number of bytes (up to 32) to another connected device in a lazy way with help
of single digital pin.
Protocol doesn't require main device to use interrupt, timer or any other hardware means - 
all is needed a single dedicated pin, and no timing constraints, user code can hang for extended period - 
and it will not affect this communication protocol.

---------------


Yes — there is a real niche for this kind of protocol, especially in the hobbyist / embedded / ultra-low-complexity Arduino ecosystem.

What you’re describing is essentially a:

* **single-wire, non-time-critical, cooperative communication protocol**
* optimized for:

  * AVR-class MCUs
  * no interrupts
  * no timers
  * no UART/SPI/I2C peripherals
  * tolerant of arbitrarily long pauses in user code
  * tiny RAM/flash footprint
  * “eventually delivered” small packets

That combination is unusual and valuable.

The key thing is not raw performance — it’s the programming model.

---

# Why existing protocols don't fully solve this

Most Arduino communication approaches assume at least one of:

| Protocol       | Requires timing?           | Interrupts useful/required? | Hardware peripheral? | Robust if main loop stalls? |
| -------------- | -------------------------- | --------------------------- | -------------------- | --------------------------- |
| UART           | Yes                        | Usually yes                 | UART                 | No                          |
| I2C            | Yes                        | Hardware state machine      | TWI                  | No                          |
| SPI            | Yes                        | Hardware SPI                | SPI                  | No                          |
| OneWire        | Very timing sensitive      | Usually                     | No                   | No                          |
| SoftwareSerial | Extremely timing sensitive | Yes                         | No                   | No                          |
| CAN            | Hardware controller        | Yes                         | CAN peripheral       | Usually                     |
| LIN            | UART timing                | UART                        | UART                 | No                          |

Your proposal is different because it explicitly targets:

> “The application may freeze/hang/do long blocking work and communication still remains valid.”

That is rare.

---

# Where this is genuinely useful

## 1. Tiny cooperative embedded systems

A lot of Arduino users write code like:

```cpp
loop() {
    readSensor();
    delay(5000);
    moveMotor();
}
```

Traditional protocols break badly here.

Your protocol could continue functioning because:

* no precise timing
* no ISR dependence
* receiver simply observes pin state eventually

That is extremely beginner-friendly.

---

## 2. Debug/control side channels

Very useful for:

* diagnostics
* watchdog communication
* configuration buses
* low-rate telemetry
* fault reporting

Especially:

* Tiny85 ↔ Mega
* Nano ↔ Nano
* low-pin-count systems

---

## 3. Deterministic industrial-ish systems

Some people deliberately avoid:

* interrupts
* timer ISR jitter
* hardware UART complexity

especially in:

* motor control
* audio
* bit-banged signal generation
* retrocomputing

A “lazy protocol” avoids hidden asynchronous behavior.

---

## 4. Sleep-heavy ultra-low-power nodes

If the protocol:

* can idle statically
* wake via pin change
* avoid timers

then it becomes attractive for battery systems.

---

# The biggest technical challenge

The hard problem is:

## How do you eliminate timing constraints?

Communication fundamentally requires *some* synchronization.

So one of these must exist:

| Method                | Likely fit           |
| --------------------- | -------------------- |
| Handshake states      | Excellent            |
| Level-based signaling | Excellent            |
| Clock stretching      | Possible             |
| Edge acknowledgment   | Excellent            |
| Self-clocking pulses  | Less aligned         |
| Manchester encoding   | Too timing-dependent |

Your concept sounds most compatible with:

# Asynchronous handshake protocol

Like old parallel-port or hardware bus handshakes.

Example:

```text
Sender raises line
Receiver notices eventually
Receiver pulls ACK somehow
Sender proceeds
```

But with one wire you probably need:

* open-drain/shared line
* pull-up resistor
* dominant/recessive states

Very similar philosophically to:

* CAN arbitration
* OneWire electrical layer
* asynchronous hardware handshakes

---

# The strongest selling point

The strongest value proposition is probably:

> “Works correctly even if loop() blocks for seconds.”

That is genuinely unusual.

Many Arduino users struggle with:

* `millis()` state machines
* ISR bugs
* SoftwareSerial corruption
* timing interactions

You are effectively offering:

* reliability over speed
* simplicity over throughput
* eventual consistency over realtime guarantees

That is a legitimate design space.

---

# Throughput expectations

You mentioned:

* ≤32 bytes
* single pin
* no timing constraints

That likely means:

* very low bandwidth
* perhaps 10–1000 bytes/sec depending on implementation

But many applications do not care.

Examples:

* commands
* status flags
* configuration
* watchdog events
* menu/UI communication
* distributed sensors

---

# Potential adoption barriers

## 1. People instinctively optimize for speed

Many hobbyists ask:

> “Why not UART?”

So you’d need very clear positioning.

Not:

> “better serial”

But:

> “communication for blocking/non-realtime sketches.”

---

## 2. Hard to explain

The concept is subtle because:

* “no timing constraints” sounds impossible
* people assume communication requires realtime behavior

You’d need:

* diagrams
* examples
* demonstrations under blocking delays

---

## 3. Existing users often don't realize they need it

The ideal users are people suffering from:

* ISR conflicts
* timing bugs
* SoftwareSerial instability
* delay()-heavy sketches

But they may not know this category exists.

---

# Where I think this could succeed

If packaged as:

## A tiny Arduino library

with:

* 2-function API
* tiny memory footprint
* AVR-first
* examples for Tiny/Nano/Mega

then yes, it could gain traction.

Especially if you demonstrate:

```cpp
delay(10000);
```

on both nodes while communication still succeeds.

That demo is compelling.

---

# Architectural advice

I would strongly recommend emphasizing:

## NOT a transport protocol

Instead market it as:

* cooperative mailbox bus
* asynchronous latch protocol
* deferred-delivery signaling
* resilient single-wire messaging

The mental model matters.

---

# Interesting technical directions

Some potentially novel properties:

## 1. Transactional semantics

Instead of streaming bytes:

* send “message objects”
* atomic delivery
* mailbox behavior

Very suitable for your constraints.

---

## 2. Crash tolerance

If one MCU resets:

* protocol recovers automatically

That would be a major differentiator.

---

## 3. Multi-drop bus

Single-wire shared bus with:

* dominant low
* pull-up high
* collision detection

could become extremely interesting.

---

# Existing nearby concepts

Closest conceptual relatives are probably:

* asynchronous hardware handshakes
* mailbox registers
* CAN dominant/recessive bus
* OneWire electrical topology
* RS485 token systems
* delay-tolerant networking (philosophically)

But your combination of:

* single wire
* no timing assumptions
* no interrupts
* no peripherals
* AVR friendliness

is distinctive.

---

# Bottom line

Yes — there is likely a meaningful niche for this.

Not as:

* a high-speed protocol
* UART replacement
* mainstream bus standard

But as:

* an ultra-simple resilient control/message channel
* for blocking cooperative Arduino code
* where reliability and simplicity matter more than bandwidth

The key is whether the implementation truly delivers:

* zero timing assumptions
* no ISR dependence
* robust recovery
* simple API
* tiny footprint

If it does, that’s a genuinely uncommon and useful capability in the Arduino world.
