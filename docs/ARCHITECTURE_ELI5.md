# Saltmarch Architecture, Explained Like You're 5

*(The grown-up version is [ARCHITECTURE.md](ARCHITECTURE.md).)*

## The magic notebook

Imagine you're playing with a big LEGO island. Instead of remembering what
the island looks like, you keep a little notebook and write down **every
single thing you do**, in order:

> 1. Put a house here.
> 2. Put a fishing hut there.
> 3. Sell 10 fish.

Here's the magic trick: if you knock the whole island over, you don't need
a photo of it. You just start over with the same LEGO box and **redo your
notebook, line by line** — and you get *exactly* the same island back.
Every time. That notebook IS the save file. That's the whole secret of
Saltmarch.

## The metronome

For the trick to work, the game can't ever say "ummm, about three seconds
later..." — it has to be exact. So the game world moves like a metronome:
**tick, tick, tick**, ten ticks every second, and things only ever happen
*on* a tick. Fast computer, slow computer, today, next year — same ticks,
same notebook, same island. (The pictures on screen glide smoothly between
ticks, but that's just drawing. The drawing is never allowed to touch the
LEGO.)

## One door into the toy room

Everything that changes the world has to walk through **one single door**
(the grown-ups call it the command funnel). Build a house? Through the
door. Sell fish? Through the door. No sneaking through windows! Because if
even one change sneaks past the door, it doesn't get written in the
notebook, and then redoing the notebook builds the *wrong* island — and
the magic trick breaks.

There's even a test button (F9) that secretly rebuilds the whole island
from the notebook and checks it matches. If it doesn't, someone snuck
through a window, and we go find them.

## Playing with friends

How do two kids play together? Easy: **they read the same notebook.** If
your friend has the same LEGO box and the same notebook, they build the
same world you do. Playing together just means taking turns writing lines
in one shared notebook — someone (the "host") decides whose line goes
first, and that's it. That's the entire multiplayer.

Some house rules:

- **Your island is YOUR island.** Nobody can write "put a house on YOUR
  island" in the notebook — the door checks whose name is on the line and
  says "nope, not yours."
- **Only boats visit.** Friends never touch your island. They send boats,
  and boats can only drop things off at your harbor — and only if your
  harbor sign says "visitors welcome."
- **Ghost boats.** Before real playing-together exists, everyone just
  mails around little postcards saying "my boat left here, going there,
  carrying fish." Your game draws everyone else's boats as friendly
  see-through ghosts sailing by. You can't touch them, but the ocean feels
  alive. And a robot can mail postcards too — from the outside, a robot
  and a friend look exactly the same.

## The shopkeeper

There's an island shopkeeper who buys your fish. She's not a magic vending
machine — she has **her own piggy bank and her own shelves**. If you dump
a mountain of fish on her, her shelves fill up and she starts paying less
("I have SO much fish already..."). Wait a while and prices drift back.
If her piggy bank is empty, she just says "can't buy today, sorry." She
plays by all the same rules you do — same door, same notebook, same ticks.

## The buttons ask nicely

The buttons and menus on screen never grab the LEGO themselves. A button
just writes a polite note — "please build a house at row 3" — and slips it
through the door. If the door says no, the button gets a note back saying
*why* ("not enough gold!") and shows you right where you clicked. And
because buttons only ever *look* at a frozen photo of the world (never the
real thing), a button can't ever mess up the magic trick — even by
accident.

## What happens while you sleep

When there's finally a computer in the clouds running the world all night,
it isn't doing anything fancy. It's just the metronome, still going:
tick, tick, tick, writing the notebook. When you wake up and log in, you
replay the lines you missed — super fast — and you're caught up. Nobody
gets cheated, because there's only ever **one notebook and one set of
rules**, awake or asleep.

## The whole thing on one hand

1. 📓 Write everything down in order.
2. ⏱️ Move only on metronome ticks.
3. 🚪 All changes go through one door.
4. ⛵ Friends are just other people writing in the notebook — and only
   boats visit.
5. 🔁 Lose everything? Replay the notebook. It always comes back the same.
