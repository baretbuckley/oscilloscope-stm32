; gf2 reads this from the folder it starts in, the Commands tab shows these
; as buttons. `just debug` starts pyocd and gf2 already connected; Connect
; is for when gf2 runs on its own next to `just gdbserver`
[commands]
Connect=file build/Debug/oscilloscope-stm32.elf;target extended-remote :3333;load;monitor reset halt;break shell_grabbed;continue
Next grab=continue
Reset=monitor reset halt;continue
