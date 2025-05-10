to compile:

```
git clone https://github.com/quickkennedy/AviaozinhoAchievements-linux
cd AviaozinhoAchievements-linux/Quake
make USE_SDL2=1 -j$(nproc)
```

then take the `quakespasm` executable and move it next to your `id1` folder.
then you can launch the game by launching `quakespasm`

# more info

this project currently takes all the piping code that gives the game achievements and a launcher and removes it. it also corrects for BDD3's folder structure.

# todo

- add back / fix pipes to work on linux
- make linux compatible launcher
