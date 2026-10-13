# LAB X-16 — Lore Summary & Design Guide

LAB X-16 is a line of mixer effects, not an instrument, so its lore is a map rather than a single room. The stage
is **Yantar**: the factory and Laboratory X-16 beneath it on one side, Professor Sakharov's mobile laboratory
across the swamp on the other, and beyond the Red Forest the Radar whose antennas carry the Rainbow signal.
Faction A, *The Deep Zone*, is the factory and the lab. Faction B, *The Ethereal Signal*, is the bunker and the
antennas. Every effect names the Zone mechanism it borrows; several also borrow a mechanism from a field far from
audio (epidemiology, flocking, auctions, Hebbian learning, reaction-diffusion, decay chains, one-time pads,
weather, rigid bodies, genetic algorithms). Concept leads; realism does not get a vote.

---

## 1. Laboratory X-16 and the Miracle Machine

* **Location and layout.** Level id `l08u_brainlab`, beneath the Production complex at Yantar, built near Lake
  Yantar before the 2006 incident. A secure underground bunker: access halls, a research wing where test subjects
  were kept, and a main circular chamber shaped like a silo housing the emitter's mechanisms. The chamber has
  several levels, each with a switch; the topmost switch shuts the emitter down. A short corridor off the top
  level leads to the controller's den. The only way out is a structural fault in the den's basins into a drainage
  tunnel network with a manhole to the surface.
* **The emitter.** X-16 researched one of the Group's side discoveries: living creatures can generate directed
  psy-emissions. A biological field emitter, essentially a giant brain, was grown with equipment from Lab X-18
  and kept in a stasis pod; the antenna was hidden in one of the factory buildings. The prototype is a
  **Kaymanov emitter**, presumably named after the Doctor. In *Clear Sky* it is called the **Miracle Machine**
  because it made small-scale emissions, not lethal away from the complex.
* **The cooling failure.** A malfunction in the cooling system (September 2011) raised the output and the
  emissions became uncontrolled, zombifying anyone salvaging near the factory. Sakharov's team traced the cause
  to the faulty cooling of an unidentified device under the complex, and sent Scar and Lefty's group to restart
  the cooling pumps from outside the lab. In *Shadow of Chernobyl* a Monolith squad later reactivated the emitter
  to full power and never came out; the Marked One, wearing Sakharov's psi-helmet, climbed the chamber and shut
  it down, finding Ghost's body and the X-16 documents in the controller's den.
* **Inside.** Zombified stalkers, snorks, a lone controller, and in the tunnels more snorks and a pseudogiant.
  Small fires with bones among the wood, bodies of stalkers, military and Monolith. There is no psy-field inside
  the lab itself; the room with the Miracle Machine kills instantly without the helmet.

## 2. Psy-emissions

* Code `zone_field_psychic`. A radiation that affects the psyche rather than the body, believed to be connected to
  the noosphere, discovered while studying the distortion of signals. Symptoms: dizziness, headaches, voices and
  noise in the head, hallucinations; prolonged exposure means mental illness, permanent brain damage, death or
  zombification. Sakharov: "It's the psy-field. Everyone who is affected by it loses his mind and turns into a
  zombie."
* Presentation: a yellow screen tint in *Shadow of Chernobyl* and *Clear Sky*, blue in *Call of Pripyat*, none in
  *Heart of Chornobyl*, where only voices grow louder until hallucinated stalkers attack. Optical distortion of
  the sky behind a field. In *Shadow of Chernobyl* the player has a **mental strength** bar that drains in a field,
  regenerates outside it, and kills at zero. Controllers project their own fields and directed bursts.
* Protection: shorter exposure; the SEVA suit; the Moonlight and Stone Flower artifacts; the **Psy-block** drug
  (one minute of near immunity); Anabiotics to sleep through an emission; and the **psi-helmet**, Sakharov's
  prototype in two variants, *Functional* and *Not adjusted*, depending on whether Kruglov or Semenov survived
  the measurements that calibrated it.
* Apparitions: the psy-field in the Red Forest conjures mutant phantoms that vanish when they "attack", costing a
  little health; near the Scorcher the player is attacked by them constantly until the antennas are shut down.

## 3. Yantar and the mobile laboratory

* A lake that drained when the factory's construction crew caused an accident underground, leaving a swamp
  "morbid and sodden with death". Zombified stalkers wander the whole sector.
* **Sakharov's mobile science lab**: a bunker air-dropped into the swamp under military protection; an airlock
  where weapons are stowed; Sakharov's office, a room with beds, a hall with Professor Kruglov. Sakharov buys
  artifacts and mutant parts, sells equipment and gives the psi-helmet. Faction B's visual anchor: a sterile,
  humming laboratory with instruments and displays, the clean counterpart of the factory.

## 4. The Radar, the Brain Scorcher and Rainbow

* A massive installation that looks like a radar but was built as a mind-altering antenna to suppress aggression,
  codenamed **Rainbow** (Радуга), designed in Lab X-8 in Pripyat and adapted by the Group with **Lab X-10** in a
  bunker beneath it. Five antennas; by 2012 one is damaged and leans. The C-Consciousness switched it on to block
  the way to the centre of the Zone, turning trespassers into zombies or Monolith recruits.
* The structure was inspired by **Duga-1**, the Soviet over-the-horizon radar near Chornobyl known as the Russian
  Woodpecker for the tapping of its signal (about 10 Hz); a BBC documentary speculated it was a mind-control
  transmitter. The Scorcher's ambience ("psy antenna voices") is reused in the plant's hallways.

## 5. X-19, C-Consciousness and the Noosphere

* Laboratory X-19 lies inside the Chornobyl NPP (the *S.T.A.L.K.E.R. 2* Interactive Almanac separates it from
  X-10, which the SoC diaries also call "X19"). It hosts **C-Consciousness**, many minds fused into one, and the
  **Wish Granter** hologram; Strelok chooses to join them or destroy them (user correction, LAB X-19 project).
* **Noosphere**: the Earth's informational field. Lab X-3 tried to move human consciousness into it to achieve
  immortality (LAB X-3 lore summary).

## 6. Zone field mechanics and objects used by the effects

| Element | Effect that borrows it |
|---|---|
| The Kaymanov emitter and its cooling pumps | KAYMANOV |
| `zone_field_psychic`, mental strength, voices, the tints | PSY-FIELD |
| Radiation and detectors | DOSIMETER |
| The silo chamber, its levels, the drainage tunnels | SILO |
| Anomalies (Springboard, Whirligig, Vortex, Comet, Jelly, Burner, Bubble, Electro) and the emission as clock | ANOMALY |
| Zombification spreading from Yantar; the psi-helmet as immunity | CONTAGION |
| Emissions: sirens and radio warnings, shelter, Anabiotics | EMISSION |
| Apparitions that vanish when struck; Ghost | APPARITION |
| Monolith fighters chanting as one, scattering under fire | ZEALOTS |
| The 100 Rads bar, Barkeep and Sidorovich, bandits' cut | 100 RADS |
| Reactor 4 under its sarcophagus | SARCOPHAGUS |
| Poltergeists and the Burer hurling objects | POLTERGEIST |
| The mutants bred in X-18; the snork | SNORK |
| The Gauss rifle, the Monolith sniper on the antenna | GAUSS |
| The psy-dog's illusory pack | PSY-DOG |
| Rainbow: five antennas, one dead | RAINBOW |
| C-Consciousness and the Wish Granter | WISH GRANTER |
| Duga-1, the Woodpecker, the psy antenna voices | CARRIER |
| Minds fused into one; the Noosphere | C-CONSCIOUSNESS |
| Artifacts forming in anomalies after emissions; Moonlight, Battery, Fireball, Bubble, Stone Flower, Compass | ARTIFACT |
| Radio chatter, the Monolith's jammer in the Pripyat kindergarten, UVB-76 (real-world shortwave) | NUMBERS STATION |
| The Oasis anomaly that heals | OASIS |
| The Zone's weather and the Clear Sky faction | CLEAR SKY |

## 7. Naming

Bilingual panel titles as in the siblings (`EMITTER // ИЗЛУЧАТЕЛЬ`, `PEERS // СВЯЗЬ`). Effect names and their
Cyrillic subtitles are in `docs/EFFECTS.md`. Presets are named after places, people, objects and events of the
Zone; a few real-world anchors (Duga-1, UVB-76) are kept because the games borrowed them first.

---

## Sources & Reference Material

* **Community database** (read 2026-10-12 through the wiki's API; the pages block plain fetches):
  [Lab X-16](https://stalker.fandom.com/wiki/Lab_X-16), [Psy-emission](https://stalker.fandom.com/wiki/Psy-emission),
  [Brain Scorcher](https://stalker.fandom.com/wiki/Brain_Scorcher), [Yantar](https://stalker.fandom.com/wiki/Yantar),
  [Kaymanov emitter](https://stalker.fandom.com/wiki/Kaymanov_emitter), [Psi-helmet](https://stalker.fandom.com/wiki/Psi-helmet),
  [Production complex](https://stalker.fandom.com/wiki/Production_complex). There is no Miracle Machine page of its own.
* **Sibling summaries:** `LAB X-3\lab_x3_lore_summary.md`, `LAB X-10\lab_x10_lore_summary.md`,
  `LAB X-0\lab_x0_lore_summary.md`, and the X-19 correction recorded in `LAB X-19\LAB_X19_HANDOFF.md`.
* **Real-world anchors:** Duga-1 (Wikipedia, "Russian Woodpecker"), UVB-76 (Wikipedia, "The Buzzer").
* No game audio is part of the plugins or the repository; the effects line reads no sample library at all.
