# RC522 custom chip (Wokwi)

`rc522.chip.wasm` đã được biên dịch sẵn. Khi sửa `rc522.chip.c`, biên dịch lại bằng clang có target wasm32:

```sh
clang --target=wasm32-unknown-wasi -ffreestanding -nostdlib -O2 -Wall -Wno-unused-function \
  -Wl,--import-memory -Wl,--export-table -Wl,--no-entry -Wl,--allow-undefined \
  -o rc522.chip.wasm rc522.chip.c
```

Hoặc dùng `wokwi-cli chip compile rc522.chip.c`.
