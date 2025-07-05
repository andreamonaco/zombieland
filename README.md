__What is Zombieland?__

Zombieland is a working title for a game, a little MMO about the struggle for
survival of humans against zombies.

![Screenshot](https://raw.githubusercontent.com/andreamonaco/zombieland/refs/heads/main/screenshot1.png)
![Screenshot](https://raw.githubusercontent.com/andreamonaco/zombieland/refs/heads/main/screenshot2.png)
![Screenshot](https://raw.githubusercontent.com/andreamonaco/zombieland/refs/heads/main/screenshot3.png)
![Screenshot](https://raw.githubusercontent.com/andreamonaco/zombieland/refs/heads/main/screenshot4.png)
![Screenshot](https://raw.githubusercontent.com/andreamonaco/zombieland/refs/heads/main/screenshot5.png)
![Screenshot](https://raw.githubusercontent.com/andreamonaco/zombieland/refs/heads/main/screenshot6.png)
![Screenshot](https://raw.githubusercontent.com/andreamonaco/zombieland/refs/heads/main/screenshot7.png)
![Screenshot](https://raw.githubusercontent.com/andreamonaco/zombieland/refs/heads/main/screenshot8.png)
![Screenshot](https://raw.githubusercontent.com/andreamonaco/zombieland/refs/heads/main/screenshot9.png)
![Screenshot](https://raw.githubusercontent.com/andreamonaco/zombieland/refs/heads/main/screenshot10.png)



__Where can I find the latest version of Zombieland?__

There are no released versions yet.  Just build from the latest commit, since
new features and bugfixes happen daily.



__On which platforms does Zombieland run?__

Zombieland is written in portable C and uses SDL2, SDL2_image, SDL2_ttf and
SDL2_mixer for multimedia, so that part is fairly portable.
Networking code uses the Posix API, so consult the docs of your platform about
that.
The configure and build toolchain is autotools, but building is easy enough that
you can do it yourself.



__How can I build Zombieland?__

If you have the dependencies installed (including the autotools), just do the
usual from the directory of the repository:

 $ autoreconf -fi && ./configure && make



__I don't have the autotools...__

You are truly the lazy type!  Then these commands will probably suffice:

 $ cc -o zombieland client.c zombieland.c -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer
 $ cc -o zombielandd server.c zombieland.c malloc.c -lSDL2



__How can I play Zombieland?__

Start the server (zombielandd) on some computer, then start the client
(zombieland) with two arguments: the address of the server and any username you
like.  Optionally, pass a third argument with a number between 0 and 6 to choose
your appearance; see the file character.png to preview each look, numbered from
0 to 6 going down.

Move around with the arrows, fire with F, stab with R to hit enemies nearby with
the knife, interact with objects (reading signs and so on) with Space and close
the game with Esc.  Any number of players can shoot at each other and at the
zombies until they die.
You can also open and close the inventory with Q; you can collect rotten meat
dropped by zombies in your bag.  If you find a searchable object in the
environment, a symbol with a bag will appear on that object; pressing Q will
then open both your inventory and that bag; with Space you can select an object
and move it between the two inventories.



__Can I test Zombieland locally?__

Yes.  After building, start first the server (zombielandd), then the client
(zombieland) on another terminal, the latter with 127.0.0.1 and any username as
arguments (optionally a third argument to choose your appearance, see "How can I
play Zombieland?").  You can even run multiple clients on the same system.



__What is Zombieland capable to do?__

I tested Zombieland on a LAN and I confirmed that multiple players can move in
the same 2D map and fight.



__Does Zombieland work across architectures?__

It should.  Let me know if you find any issue.



__Who made the assets?__

For the maps I used the following tilesets taken from https://opengameart.org:

"Zelda-like tilesets and sprites" by ArMM1998 licensed CC0 1.0 Universal:
https://opengameart.org/content/zelda-like-tilesets-and-sprites

"RPG Urban Pack" by Kenney (www.kenney.nl) licensed CC0 1.0 Universal:
https://opengameart.org/content/rpg-urban-pack

The actual maps are made by me and retain the same license.

The player sprites (character.png), zombie sprite (NPC_test.png) and NPC sprite
(log.png) come from those same sets.

The spark effect in effects.png comes from:

"5x special effects - 2D" by rubberduck licensed CC0 1.0 Universal:
https://opengameart.org/content/5x-special-effects-2d

The icons in objects.png are made by me.

The fonts are

"Boxy Bold - TrueType Font" by Clint Bellanger (http://clintbellanger.net),
cemkalyoncu, william.thompsonj and usr_share licensed CC0 1.0 Universal:
https://opengameart.org/content/boxy-bold-truetype-font

"Digital Jots 5x5 pixel font (with katakana)" by jdm0079 licensed CC0 1.0
Universal:
https://opengameart.org/content/digital-jots-5x5-pixel-font-with-katakana

The sound effect bang_01.ogg comes from

"25 CC0 bang / firework SFX" by rubberduck licensed CC0 1.0 Universal:
https://opengameart.org/content/25-cc0-bang-firework-sfx

while knifesharpener1.flac is taken from

"Knife sharpening slice 1" by The Berklee College of Music licensed Attribution
3.0 Unported (https://creativecommons.org/licenses/by/3.0):
https://opengameart.org/content/knife-sharpening-slice-1

and reload.wav is an effect I edited starting from

"Handgun Reload Sound Effect" by zer0_sol licensed CC0 1.0 Universal:
https://opengameart.org/content/handgun-reload-sound-effect

All these assets are not final, I will probably make my own along the way, but
they show that the engine works.



__Why can't I shut down the server with Ctrl-C?__

I'm not sure, but probably reading sockets non-blockingly changes the default
handling of SIGINT.  I will look into it further.



__Does Zombieland contain any line of code written by AI?__

No!  This is a game about zombies, not written by zombies.



__How can I reach the author?__

You can write an email at monacoandrea + 94 + at + gmail + .com, where "+" is
concatenation.



__Can I donate something to the author?__

Yes, I have Patreon (https://www.patreon.com/c/andreamonaco) and Liberapay
(https://liberapay.com/andreamonaco).  Thanks for your support.
