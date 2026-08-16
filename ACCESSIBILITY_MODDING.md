# Project Hortator — Modding Guide and Recommended Mods

Morrowind has one of the largest modding communities of any game, and almost all
of it works with Project Hortator, because Project Hortator *is* OpenMW — the
accessibility features are built into the engine rather than bolted on. A mod
does not need to know anything about screen readers to work with this build.

This guide describes the mod setup used by Project Hortator's own developer.
It is entirely optional. Everything in the accessibility documentation works on
a plain, unmodded install, and if you are new to Morrowind you may prefer to
play it that way first.

**A word of warning before you start.** Modding Morrowind is fiddly, and it is
easy to end up with a broken install. Do this on a fresh copy of the game, keep
your saves backed up, and be prepared to start over. Mods are made by many
different people and are not part of Project Hortator; if a modded game
misbehaves, the mod is far more likely to be at fault than the engine.

---

## Contents

The setup happens in two stages:

1. **Install a base mod list.** This does the heavy lifting — hundreds of
   patches, fixes and improvements, installed automatically by a tool.
2. **Apply customisations on top.** This is where personal taste comes in: the
   extra mods listed further down, and one removal.

You do not have to do stage 2. Stage 1 alone gives you a complete, stable,
well-tested game.

---

## Stage 1: the base mod list

Project Hortator's developer uses **I Heart Vanilla: Director's Cut**, a list
maintained by the Modding OpenMW project. It stays close to the original game's
feel — it fixes and refines rather than replacing — which makes it a good
foundation.

Follow the official installation guide, which is kept up to date and covers
everything from the tools you need to the final launch:

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

---

## Stage 2: customisations

Modding OpenMW documents the general method here:

**[Customizing mod lists — Modding OpenMW](https://modding-openmw.com/tips/customizing-modlists/)**

It comes down to two files:

| File | What it is | Where it goes |
| --- | --- | --- |
| `luceus-custom-mods.json` | The list of extra mods, in a form `umo` can install | Anywhere; you pass it to `umo` |
| `momw-customizations.toml` | Instructions for the configurator: what to add, what to remove, and in what order | Your OpenMW configuration folder, next to `openmw.cfg` |

Both files are shared, so you can reproduce this setup exactly rather than
assembling it by hand:

- `luceus-custom-mods.json` —
  **[download the mod list](https://www.dropbox.com/scl/fi/wc2yx0mqhzdd4767v33in/luceus-custom-mods.json?rlkey=8a7rtek3uwchxin5d92o206ce&dl=1)**
- `momw-customizations.toml` —
  **[download the configurator instructions](https://www.dropbox.com/scl/fi/ntmhyv5e2v9943lzyhbb2/momw-customizations.toml?rlkey=a4ixxqwb05u9vsngjfwat8kjo&dl=1)**

Both links download the file directly rather than opening a Dropbox preview
page, so your browser should just save them. Keep the filenames as they are —
`umo` takes the list's name from the filename.

### The commands

With both files downloaded, run these in order:

```
umo.exe list add luceus-custom-mods.json
```

This adds the list to `umo`'s cache. It takes the name of the file minus the
extension, so the list is called `luceus-custom-mods`. That naming is
deliberate: if it were simply called "custom", it would collide with the list
`umo` uses for *your* own additions. Keeping it separate means you can add your
own mods later without the two interfering.

```
umo.exe install --sync luceus-custom-mods
```

This downloads and installs everything on the list. It will take a while, and
some mods may need you to be signed in to Nexus Mods.

Next, place `momw-customizations.toml` in your OpenMW configuration folder —
the one containing `openmw.cfg`, usually `Documents\My Games\OpenMW`. Then:

```
momw-configurator.exe config -v --run-validator i-heart-vanilla-directors-cut
```

This rewrites your configuration so the base list and the extra mods are all
loaded in the correct order, and runs a validator to catch mistakes. Read its
output rather than assuming it worked.

### If you want only some of these mods

Edit `momw-customizations.toml` and remove the lines for the mods you do not
want — each mod contributes a data-path line and one or more content-file lines,
and you need to remove **both** for a given mod. Then re-run the configurator
command above. Watch for dependencies; the notes below flag them.

---

## The one removal: Morrowind Interiors Project

The customisations file **removes** a mod that the base list includes.

Morrowind Interiors Project makes interiors show real sky and weather through
their windows. It is purely cosmetic, so it offers nothing to a player who is
not looking at it — and it has a concrete downside. To make interiors show the
outdoors, it marks those cells as quasi-exterior, which also gives them the
outdoor water level. In any interior whose floor sits below the world's water
height, the room floods.

The clearest example is the Six Fishes in Ebonheart, where the flooding is deep
enough to drown the innkeeper and the trainers who live there. Losing NPCs to a
cosmetic mod is a bad trade at the best of times; losing them silently, when you
cannot see the water, is worse.

All three of its plugins share one folder, so the removal takes out the folder
and all three content files together.

---

## The recommended mods

Comments below are the developer's own opinions, and are marked as such where
they are matters of taste rather than fact.

### Sound and accessibility

These matter more than usual for a player who is listening rather than looking.

**Morrowind Acoustic Overhaul** — An overhaul of the game's sound effects. In
his words: *"Amazing overhaul of the sounds in the game, though some may prefer
the... unique vanilla noises."* If you have played Morrowind before and are
attached to its distinctive sound design, this will change a lot of it.

**Maxar's Dynamic Footsteps Sounds** *(maxar)* — Footstep sounds that respond to
what you are walking on, how fast you are moving, your race, and your armour,
including water depth.

**Custom Music** *(LordLuceus)* — A handful of extra music tracks, taken from
Morrowind-adjacent projects: ESO Morrowind and Skywind.

**LMM_Access** *(LordLuceus)* — A small accessibility plugin that makes Daisy's
Lua Multimark Mod (below) usable with a screen reader. Install it only if you
are installing Multimark.

### Companions and followers

**Arvesa — An Armiger's Tale** *(MasterofChim)* — *"Excellent companion mod,
highly recommended."* A fully voiced companion with a long personal questline
that runs alongside the main quest.

**Arvesa — Tamriel Rebuilt** *(Mitya Skinny)* — Extends the same companion into
the Tamriel Rebuilt landmass.

**Attend Me** *(urm)* — Makes your followers teleport with you when you use
Recall or Intervention, and it prevents the common and
maddening situation where a companion is stranded on the other side of the
province.

**Shock Centurion Companion** *(Cyberwarth)* — *"Neat little mod that lets you
order a certain centurion to stay in a specific place or patrol the area instead
of following you around everywhere."*

**Friendlier Fire** *(Sosnoviy Bor)* — Stops followers turning on you, and on
each other, after an accidental hit. Friendly blows miss or do reduced damage,
and neither you nor your companions can hurt each other with spells. This is
worth more to a blind player than to a sighted one: you cannot see exactly where
a companion is standing when you swing a weapon or place an area spell, and in
vanilla a stray hit can turn a long-running companion hostile with no warning.

**Follower Detection Util** *(Sosnoviy Bor)* — A library, not content. It does
nothing on its own; Friendlier Fire needs it. **It must load before Friendlier
Fire** — the customisations file already puts it in the right place.

### Quests and content

**AFFiliates — Guild of Mages** *(AFFA)* — *"Fun and at times absurd quest mod written by AFFA AKA Douglas Goodall, one of the original writers of Morrowind. You finally get to play both sides of the Ajira vs Galbedir war."*

**Barristers Guild — Old Ebonheart Questline** *(levanesque)* — Lets you join
the Old Ebonheart Barristers Guild. *"Haven't played through this one yet but it should
be a lot of fun."*

**OAAB Brother Juniper's Twin Lamps** *(Brother Juniper, updated by Lucevar)* —
Quests for the abolitionist underground. *"For the abolitionists. Haven't
actually played through most of these quests yet myself."*

**Red Wisdom — An Ashlander Prophecy** *(AFFA, Greatness7, Melchior Dahrk)* —
*"I was a bit disappointed with this one. Unlike Rise of House Telvanni, it's
not that the writing was bad, it's that I felt like there wasn't enough. I expected something a bit more substantial."*

**Solstheim — Tomb of the Snow Prince** *(TOTSP Team)* — A large overhaul of
Solstheim. Recommended by the Tamriel Rebuilt team.

**Vegtabill's Threads of the Webspinner** *(vegtabill)* — Makes the Morag Tong's
Sanguine item hunt completable from inside the game. In vanilla, only two of the
27 items are ever pointed out to you; the rest are carried by NPCs the game never
mentions, so the quest is effectively unfinishable without consulting a wiki or
killing people at random. This fleshes out existing dialogue topics and adds new
ones so the leads exist in-game.

The mod ships four alternative plugins and only one may be active. This setup
uses `veg-TotW-books-MSrestored.esp`, the fullest of them: the dialogue changes,
plus in-world journals and letters, plus Mephala's Skill restored to its original
form (it fortified Short Blade as well, before Bethesda changed it).

**The Popular Plague** *(AFFA, Greatness7, Melchior Dahrk, Seelof)* — A strange
disease reaches Pelagiad, and its victims cannot stop dancing. Written by Douglas
Goodall, who wrote much of the original game's dialogue. Start it by talking to
Prupius Danulus in Pelagiad. It leads somewhere considerably odder than it first
appears, and ends with a home of your own in Oblivion that you can teleport to
and from at will. Needs OAAB_Data, which is already on this list.

### Rise of House Telvanni — read this one carefully

**Rise of House Telvanni** *(Pozzo, Karpik777, bhl)* and **Rise of House
Telvanni 2.0** *(mort)*.

This is the one entry with a genuinely mixed recommendation, quoted at length
because the decision is yours:

> *"I have mixed feelings on this one. The best way I can describe it is that it
> feels very 'fanfiction-like', in a bad way, and as someone who really
> appreciates lore and writing, this really bothers me. On the other hand, it
> introduces some ridiculously overpowered items and spells (also debatable whether that's a
> good thing — these really are insanely overpowered) and some fun combat
> encounters. So your mileage may vary; dropping it would be a totally valid
> choice."*

On the 2.0 update: *"Apparently the older version of this was even more
ridiculous, complete with a self-insert character. This fixes some of it, but
not enough."*

**If you drop it, you must also drop everything that depends on it**, or the
game will fail to load. That means all of the following:

- `Rise of House Telvanni.esm` and `ROHT_2_0_8.ESP` (the mod itself)
- `TL_DukeFallbackToIlmeni.omwaddon` — the Duchess Ilmeni Dren compatibility
  patch, which exists only to reconcile this mod with Brother Juniper's Twin
  Lamps
- `UL_3.5_RoHT_1.52_Add-on.esp` — the Uvirith's Legacy compatibility addon

Remove their data-path lines from `momw-customizations.toml` as well as their
content lines. Uvirith's Legacy itself, and its Tamriel Rebuilt addon, are
unaffected and can stay.

### Uvirith's Legacy and the Telvanni stronghold

**Uvirith's Legacy** *(Stuporstar)* — A vast expansion of the Telvanni player
stronghold, Tel Uvirith. *"Just an excellent mod. Still holds up ten years after its
last update. The tower can be a bit difficult to navigate for blind players at
times, but it's worth it."*

**Building Up Uvirith's Legacy** *(Acheron & Artimis Fowl)* — *"Also quite good,
and mostly passive. It's nice to watch a little town spring up around your
tower."*

**LGNPC — Tel Uvirith** — *"Integrates exceptionally well with Uvirith's Legacy
and makes your retainers, well, less generic and into actual characters."*

**Null's Minor Patches** *(NullCascade)* — Provides the Uvirith's Legacy /
Tamriel Rebuilt compatibility addon used here.

### Difficulty and rebalancing

**Tribunal Rebalance**, **Bloodmoon Rebalance**, and **Beware the Sixth House**
*(all by mort)* — *"These last three are all by the same person so the rebalance
is cohesive. Makes the Sixth House the most difficult content in the game, as it
should be, instead of random werewolves."*

Because they are by one author and designed together, treat them as a set.

**Expansion Delay** *(Half11)* — Stops the Tribunal and Bloodmoon content from
ambushing you at level one, so the expansions begin when you go looking for
them. *"A must-have in my opinion."*

**Speechcraft Rebalance** *(Aphain)* — *"A nice little mod making speechcraft a
little more usable than in vanilla."*

**Pickpocket Rebalance** *(Aphain)* — By the same author. Vanilla pickpocketing
caps your success chance below 100% no matter how skilled you are, and weights
heavily against anything valuable; this raises the cap and relaxes the weighting,
so a thief character can actually steal things worth stealing.

### World and integration

**Repopulated Creatures** *(GrumblingVomit)* — Adds Tamriel Rebuilt creatures to
Vvardenfell. *"Any mod that makes the integration between vanilla and Tamriel
Rebuilt more seamless is a good one in my book."*

**OAAB_Data** *(OAAB_Data Team)* — A shared asset library that many of the mods
above depend on. Not content in itself; install it and forget about it.

### Utility

**Daisy's Lua Multimark Mod** *(DaisyHasACat)* — *"Lets you have more than one
mark, really handy."* Vanilla Morrowind allows a single Mark location for the
Recall spell; this lifts that limit. Install **LMM_Access** alongside it to make
its interface speak.

---

## If something goes wrong

Work backwards. A modded Morrowind that misbehaves is almost always a load-order
or dependency problem, not an engine bug.

1. **Re-run the configurator with the validator** — the command in Stage 2. It
   reports missing dependencies and ordering mistakes.
2. **Check whether it happens without mods.** Launch with a plain profile. If
   the problem disappears, it is a mod.
3. **Only then report it as a Project Hortator problem** — and say which mod
   list you are using, because a bug that only appears with mods installed needs
   different information to diagnose. See **Reporting problems** in
   `ACCESSIBILITY_README.html`.

Mod authors generally welcome bug reports too, and a problem in a mod's own
content is best fixed by the person who wrote it.
