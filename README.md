# Asteroids

A recreation of the classic arcade game, made in C++23 and using a release candidate build of SFML 3.0. The game is true to the original's mechanics - you navigate an asteroid field and shoot incoming asteroids while trying to avoid getting hit by any yourself.

![clip1](https://i.imgur.com/ncTquGn.gif)

Controls are Arrow Keys/WASD to move, space to shoot.

## Installation

This game depends on [SFML](https://www.sfml-dev.org/) 3.0 Release Candidate 2; but should work on fully-released SFML 3.0. There are no additional dependencies other than C++23; however this repo makes heavy use of certain C++23 features (such as the standard library module) which still see some heavy implementation divergence. It was written and compiles on the latest version of MSVC, but if you use another build system your mileage may vary depending on what your compiler supports.

![clip2](https://i.imgur.com/vwJ2mea.gif)
