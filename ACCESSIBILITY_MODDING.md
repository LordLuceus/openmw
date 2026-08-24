# Project Hortator — Modding Guide and Recommended Mods

Morrowind has one of the largest modding communities of any game, and almost all
of it works with Project Hortator, because Project Hortator *is* OpenMW — the
accessibility features are built into the engine rather than bolted on. A mod
does not need to know anything about screen readers to work with this build.

This guide is a list of mods I recommend, with my reasons. It is entirely
optional. Everything in the accessibility documentation works on a plain,
unmodded install, and if you are new to Morrowind you may prefer to play it that
way first.

**Read the descriptions before you install anything.** The mods below do very
different things. Some are small conveniences you will never think about again;
others add tens of hours of new land and quests, or change the difficulty of the
whole game. Installing everything here is not the intended use of this list — it
is my own taste, and taste is the one thing you should not copy. Pick what you
actually want.

**A word of warning before you start.** Modding Morrowind is fiddly, and it is
easy to end up with a broken install. Do this on a fresh copy of the game, keep
your saves backed up, and be prepared to start over. Mods are made by many
different people and are not part of Project Hortator; if a modded game
misbehaves, the mod is far more likely to be at fault than the engine.

---

## Contents

- [Where to start: a base mod list](#where-to-start-a-base-mod-list)
- [How to install the mods below](#how-to-install-the-mods-below)
- [Quality of life and accessibility](#quality-of-life-and-accessibility)
- [Rebalancing and difficulty](#rebalancing-and-difficulty)
- [Companions](#companions)
- [New quests and content](#new-quests-and-content)
- [Large additions](#large-additions)
- [Shared libraries](#shared-libraries)
- [If something goes wrong](#if-something-goes-wrong)

The sections are ordered by how much they change your game. If you only want the
game to be a little smoother, the first section is all you need and you can stop
reading there. The later sections add substantial amounts of new content.

---

## Where to start: a base mod list

Before adding anything from this list, it is worth installing a curated base mod
list. These do the heavy lifting — hundreds of patches, bug fixes and small
improvements that nobody wants to assemble by hand.

I use **I Heart Vanilla: Director's Cut**, maintained by the Modding OpenMW
project. It stays close to the original game's feel — it fixes and refines
rather than replacing — which makes it a good foundation. There are other lists
if you want something more transformative; Modding OpenMW documents them all.

**[I Heart Vanilla: Director's Cut — Windows installation guide](https://modding-openmw.com/guides/auto/i-heart-vanilla-directors-cut/windows#selected-mod-list)**

Work through that guide to the end. When you have finished, you should be able
to launch the game and play. **Confirm that before going any further** — if
something is wrong, you want to find out now, while there is only one thing that
could have caused it.

A note on the tools: the guide has you install `umo` (which downloads and
installs mods) and `momw-configurator` (which writes the correct load order into
your OpenMW configuration). Both are command-line programs, which in practice
makes them *more* screen-reader-friendly than a graphical mod manager, not less.
You will be typing commands rather than hunting for checkboxes.

### Use Project Hortator's engine, not the one the guide installs

The guide will set up an OpenMW installation of its own. You want the mods, but
you want to run them with **this** build. Point Project Hortator at the
configuration the guide produced, rather than letting it create its own — see
**Installing** in `ACCESSIBILITY_README.html`, and keep in mind that both the
guide's OpenMW and this one read the same configuration folder
(`Documents\My Games\OpenMW`).

### One mod I remove from it

I take **Morrowind Interiors Project** out of the base list. It makes interiors
show real sky and weather through their windows, which offers nothing to a
player who is not looking at it — and it has a concrete downside. To show the
outdoors, it marks those cells as quasi-exterior, which also gives them the
outdoor water level. In any interior whose floor sits below the world's water
height, the room floods.

The clearest example is the Six Fishes in Ebonheart, where the flooding is deep
enough to drown the innkeeper and the trainers who live there. Losing NPCs to a
cosmetic mod is a bad trade at the best of times; losing them silently, when you
cannot see the water, is worse. Note that it ships three plugins in one folder,
so removing it means removing all three.

---

## How to install the mods below

Modding OpenMW documents how to add your own mods on top of a base list:

**[Customizing mod lists — Modding OpenMW](https://modding-openmw.com/tips/customizing-modlists/)**

The short version: you keep your own list of extra mods in a file `umo` can
install from, and a customisations file telling `momw-configurator` what to add,
remove, and in what order. Follow that page rather than copying commands from
here — it is maintained by the people who wrote the tools, and it explains the
load-order rules that keep a modded install working.

Two things to watch for as you add mods from this list:

- **Dependencies.** Some entries need another mod present, and a few need to
  load in a specific order. Where that applies, the entry says so.
- **Alternative plugins.** A mod sometimes ships several variant plugins of
  which only one may be active. Read the mod's own documentation.

After every change, re-run the configurator with its validator and read the
output rather than assuming it worked.

---

## Quality of life and accessibility

Nothing here adds new content. These make the existing game smoother, and the
first few matter more than usual for a player who is listening rather than
looking.

**Maxar's Dynamic Footsteps Sounds** *(maxar)* — Footstep sounds that respond to
what you are walking on, how fast you are moving, your race, and your armour,
including water depth.

**Morrowind Acoustic Overhaul** — An overhaul of the game's sound effects.
Amazing overhaul of the sounds in the game, though some may prefer the... unique
vanilla noises. If you have played Morrowind before and are attached to its
distinctive sound design, this will change a lot of it.

**Daisy's Lua Multimark Mod** *(DaisyHasACat)* — Vanilla Morrowind allows a
single Mark location for the Recall spell; this lifts that limit. Lets you have
more than one mark, really handy. Install **LMM_Access** alongside it.

**[LMM_Access](https://www.dropbox.com/scl/fi/6kvyeink8keug3xfsaotu/LMM_Access.7z?rlkey=61cxxfut7onhrvpo40ej7awpr&dl=1)** *(LordLuceus)* — A small accessibility plugin that makes Daisy's
Lua Multimark Mod usable with a screen reader. It speaks the selected mark and
its position in the list, how many mark slots are free, the key hints, and the
text as you type when renaming. It adds no keys of its own and does not change
Multimark, which keeps all of its own navigation.

Only useful if you are installing Multimark, and it needs this build — it relies
on an interface that Project Hortator adds to OpenMW, so it will not load on a
standard OpenMW install. It is not on Nexus Mods, hence the direct download.

**Attend Me** *(urm)* — Makes your followers teleport with you when you use
Recall or Intervention, which prevents the common and maddening situation where
a companion is stranded on the other side of the province.

**Friendlier Fire** *(Sosnoviy Bor)* — Stops followers turning on you, and on
each other, after an accidental hit. Friendly blows miss or do reduced damage,
and neither you nor your companions can hurt each other with spells. This is
worth more to a blind player than to a sighted one: you cannot see exactly where
a companion is standing when you swing a weapon or place an area spell, and in
vanilla a stray hit can turn a long-running companion hostile with no warning.
**Needs Follower Detection Util**, loaded before it.

**Vegtabill's Threads of the Webspinner** *(vegtabill)* — Makes the Morag Tong's
Sanguine item hunt completable from inside the game. In vanilla, only two of the
27 items are ever pointed out to you; the rest are carried by NPCs the game never
mentions, so the quest is effectively unfinishable without consulting a wiki or
killing people at random. This fleshes out existing dialogue topics and adds new
ones so the leads exist in-game.

It ships four alternative plugins and only one may be active. I use the fullest
of them, which combines the dialogue changes with in-world journals and letters,
and restores Mephala's Skill to its original form — it fortified Short Blade as
well, before Bethesda changed it.

**Speechcraft Rebalance** *(Aphain)* — A nice little mod making speechcraft a
little more usable than in vanilla.

**Pickpocket Rebalance** *(Aphain)* — By the same author. Vanilla pickpocketing
caps your success chance below 100% no matter how skilled you are, and weights
heavily against anything valuable; this raises the cap and relaxes the weighting,
so a thief character can actually steal things worth stealing.

**Expansion Delay** *(Half11)* — Stops the Tribunal and Bloodmoon content from
ambushing you at level one, so the expansions begin when you go looking for
them. A must-have in my opinion.

---

## Rebalancing and difficulty

These change how hard the game is. They do not add new places to go, but they do
change the experience throughout.

**Tribunal Rebalance**, **Bloodmoon Rebalance** and **Beware the Sixth House**
*(all by mort)* — These three are all by the same person so the rebalance is
cohesive. Makes the Sixth House the most difficult content in the game, as it
should be, instead of random werewolves. Because they are designed together,
treat them as a set.

---

## Companions

Followers who travel with you. These add dialogue and questlines of their own.

**Arvesa — An Armiger's Tale** *(MasterofChim)* — Excellent companion mod,
highly recommended. A fully voiced companion with a long personal questline that
runs alongside the main quest.

**Arvesa — Tamriel Rebuilt** *(Mitya Skinny)* — Extends the same companion into
the Tamriel Rebuilt landmass. Needs both Arvesa and Tamriel Rebuilt.

**Shock Centurion Companion** *(Cyberwarth)* — Neat little mod that lets you
order a certain centurion to stay in a specific place or patrol the area instead
of following you around everywhere.

---

## New quests and content

Each of these adds a self-contained questline. You can install one and ignore
the rest.

**AFFiliates — Guild of Mages** *(AFFA)* — Fun and at times absurd quest mod
written by AFFA AKA Douglas Goodall, one of the original writers of Morrowind.
You finally get to play both sides of the Ajira vs Galbedir war.

**The Popular Plague** *(AFFA, Greatness7, Melchior Dahrk, Seelof)* — A strange
disease reaches Pelagiad, and its victims cannot stop dancing. Also written by
Douglas Goodall. Start it by talking to Prupius Danulus in Pelagiad. It leads
somewhere considerably odder than it first appears, and ends with a home of your
own in Oblivion that you can teleport to and from at will. Needs OAAB_Data.

**Barristers Guild — Old Ebonheart Questline** *(levanesque)* — Lets you join
the Old Ebonheart Barristers Guild. Haven't played through this one yet but it
should be a lot of fun.

**OAAB Brother Juniper's Twin Lamps** *(Brother Juniper, updated by Lucevar)* —
Quests for the abolitionist underground. Haven't actually played through most of
these quests yet myself.

**Red Wisdom — An Ashlander Prophecy** *(AFFA, Greatness7, Melchior Dahrk)* — I
was a bit disappointed with this one. Unlike Rise of House Telvanni, it's not
that the writing was bad, it's that I felt like there wasn't enough. I expected
something a bit more substantial.

**Rise of House Telvanni** *(Pozzo, Karpik777, bhl)* and **Rise of House
Telvanni 2.0** *(mort)* — The one entry here I am genuinely unsure about. I have
mixed feelings on this one. The best way I can describe it is that it feels very
"fanfiction-like", in a bad way, and as someone who really appreciates lore and
writing, this really bothers me. On the other hand, it introduces some
ridiculously overpowered items and spells (also debatable whether that's a good
thing — these really are insanely overpowered) and some fun combat encounters.
So your mileage may vary; skipping it would be a totally valid choice.

On the 2.0 update: apparently the older version of this was even more
ridiculous, complete with a self-insert character. This fixes some of it, but
not enough.

Be aware that other mods build compatibility patches on top of it — there is a
Duchess Ilmeni Dren patch reconciling it with Brother Juniper's Twin Lamps, and
an Uvirith's Legacy addon. If you skip Rise of House Telvanni, skip those too,
or the game will refuse to load.

---

## Large additions

These are big. Each one adds substantial new land, buildings or systems, and
between them they change the shape of a playthrough considerably. This is the
section to be most selective about.

**Uvirith's Legacy** *(Stuporstar)* — A vast expansion of the Telvanni player
stronghold, Tel Uvirith. Just an excellent mod. Still holds up ten years after
its last update. The tower can be a bit difficult to navigate for blind players
at times, but it's worth it.

**Building Up Uvirith's Legacy** *(Acheron & Artimis Fowl)* — Also quite good,
and mostly passive. It's nice to watch a little town spring up around your
tower. Needs Uvirith's Legacy.

**LGNPC — Tel Uvirith** — Integrates exceptionally well with Uvirith's Legacy
and makes your retainers, well, less generic and into actual characters.

**Solstheim — Tomb of the Snow Prince** *(TOTSP Team)* — A large overhaul of
Solstheim, recommended by the Tamriel Rebuilt team.

**Repopulated Creatures** *(GrumblingVomit)* — Adds Tamriel Rebuilt creatures to
Vvardenfell. Any mod that makes the integration between vanilla and Tamriel
Rebuilt more seamless is a good one in my book. Needs Tamriel Rebuilt.

---

## Shared libraries

Not content. These do nothing on their own, but other mods need them. Install
and forget about them.

**OAAB_Data** *(OAAB_Data Team)* — A shared asset library that a great many
mods depend on, including several above.

**Follower Detection Util** *(Sosnoviy Bor)* — Needed by Friendlier Fire. **It
must load before Friendlier Fire.**

**Null's Minor Patches** *(NullCascade)* — Provides the Uvirith's Legacy /
Tamriel Rebuilt compatibility addon.

---

## If something goes wrong

Work backwards. A modded Morrowind that misbehaves is almost always a load-order
or dependency problem, not an engine bug.

1. **Re-run the configurator with the validator.** It reports missing
   dependencies and ordering mistakes.
2. **Check whether it happens without mods.** Launch with a plain profile. If
   the problem disappears, it is a mod.
3. **Only then report it as a Project Hortator problem** — and say which mod
   list you are using, because a bug that only appears with mods installed needs
   different information to diagnose. See **Reporting problems** in
   `ACCESSIBILITY_README.html`.

Mod authors generally welcome bug reports too, and a problem in a mod's own
content is best fixed by the person who wrote it.
