#  ds4touch v1.1

Adds ds4 touchpad support (ds4vita way) to PS TV and Vita with MiniVitaTV.\
Based on [xerpi](https://github.com/xerpi "xerpi")'s [ds4vita code](https://github.com/xerpi/ds4vita "ds4vita code")

### Install

0. If updating, and there is **ds4touch.suprx** under *ur0:/tai/* folder or in taiHEN's config (*ur0:/tai/config.txt*) - remove it.
1. Copy **ds4touch.skprx** to *ur0:/tai/* folder.
2. Add **ds4touch.skprx** to taiHEN's config (*ur0:/tai/config.txt*) under **KERNEL** section:
	```
	*KERNEL
	ur0:tai/ds4touch.skprx
	```

### Limitations
- If connected several controllers, only the one connected first would be able to use touchpad
- On **PS TV** for plugin to work, you need to disable "Use Touch Pointer in Games" under quick settings.

### Download: 
https://github.com/MERLev/ds4touch/releases

### Credits
Based on [ds4vita](https://github.com/xerpi/ds4vita "ds4vita code") by [xerpi](https://github.com/xerpi "xerpi")\
All testing done by [bosshunter](https://github.com/bosshunter)
