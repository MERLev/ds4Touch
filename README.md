#  ds4touch

Adds ds4 touchpad support (ds4vita way) to PS TV and Vita with MiniVitaTV.\
Based on [xerpi](https://github.com/xerpi "xerpi")'s [ds4vita code](https://github.com/xerpi/ds4vita "ds4vita code")

### Vita with MiniVitaTv install

1. Copy **ds4touch.skprx** to *ur0:/tai/* folder
2. Add **ds4touch.skprx** to taiHEN's config (*ur0:/tai/config.txt*) under **KERNEL** section:
	```
	*KERNEL
	ur0:tai/ds4touch.skprx
	```

### PS TV install

1. Copy **ds4touch.skprx** and **ds4touch.suprx** to ur0:/tai/ folder
2. Add **ds4touch.skprx** to taiHEN's config (*ur0:/tai/config.txt*) under **KERNEL** section:
	```
	*KERNEL
	ur0:tai/ds4touch.skprx
	```
3. Add **ds4touch.suprx** to taiHEN's config (*ur0:/tai/config.txt*) under **ALL** section:
	```
	*ALL
	ur0:tai/ds4touch.suprx
	```

### Download: 
https://github.com/MERLev/ds4touch/releases

### Credits
Based on [ds4vita](https://github.com/xerpi/ds4vita "ds4vita code") by [xerpi](https://github.com/xerpi "xerpi")\
All testing done by [bosshunter](https://github.com/bosshunter)
