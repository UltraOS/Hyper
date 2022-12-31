section .real_code
BITS 16

A20_FAST_TEST_ITERATIONS: equ (1 << 5)
A20_LONG_TEST_ITERATIONS: equ (1 << 20)
%define IO_DELAY out 0x80, al

; Tries to enable A20 100 times and then gives up
; bool enable_a20()
global enable_a20
enable_a20:
    mov ecx, 100

.try_once:
    push ecx
    call do_enable_a20
    pop ecx

    test al, al
    jnz .done

    dec ecx
    jnz .try_once

.done:
    ret

do_enable_a20:
    ; Check if it was already enabled
    push dword A20_FAST_TEST_ITERATIONS
    call is_a20_enabled
    test al, al
    jnz .done

    ; Try this: https://oldlinux.superglobalmegacorp.com/Linux.old/docs/interrupts/int-html/rb-1336.htm
    mov ax, 0x2401
    int 0x15

    ; Check if BIOS worked
    mov [esp], dword A20_FAST_TEST_ITERATIONS
    call is_a20_enabled
    test al, al
    jnz .done

    ; Try enable using the 8042
    call do_enable_a20_using_8042

    ; Check if 8042 worked
    mov [esp], dword A20_LONG_TEST_ITERATIONS
    call is_a20_enabled
    test al, al
    jnz .done

    ; Try enable using fast gate
    ; Info from here: https://www.win.tue.nl/~aeb/linux/kbd/A20.html
    SYSTEM_CONTROL_PORT:      equ 0x92
    SYSTEM_CONTROL_RESET_BIT: equ (1 << 0)
    SYSTEM_CONTROL_A20_BIT:   equ (1 << 1)

    in  al, SYSTEM_CONTROL_PORT
    and al, ~SYSTEM_CONTROL_RESET_BIT
    or  al, SYSTEM_CONTROL_A20_BIT
    out SYSTEM_CONTROL_PORT, al

    mov [esp], dword A20_LONG_TEST_ITERATIONS
    call is_a20_enabled

.done:
    add sp, 4
    ret

; bool is_a20_enabled(dword: rw_iterations)
is_a20_enabled:
    mov ecx, [esp + 2]

    mov ax, fs
    push ax ; preserve fs

    xor ax, ax
    mov fs, ax ; set fs to 0x0000

    mov ax, gs
    push gs ; preserve gs

    mov ax, 0xFFFF
    mov gs, ax ; set gs to 0xFFFF

    mov eax, [fs:0x0000]
    push eax ; save old value stored at FS:0x0000

.rw_loop:
    ; write old number + 1
    inc eax
    mov [fs:0x0000], eax

    IO_DELAY

    ; read at +1 megabyte and see if it wrapped around
    mov edx, [gs:0x0010]

    cmp eax, edx

    ; values didn't match, A20 is enabled
    jne .done

    dec ecx
    jnz .rw_loop

.done:
    ; return true if we didn't timeout
    xor eax, eax
    test ecx, ecx
    setnz al

    ; restore old value
    pop edx
    mov [fs:0x0000], edx

    ; restore old segment values
    pop dx
    mov gs, dx

    pop dx
    mov fs, dx

    ret

; 8042 ports
PS2_STATUS_PORT:  equ 0x64
PS2_COMMAND_PORT: equ 0x64
PS2_DATA_PORT:    equ 0x60

; Status bits
PS2_STATUS_OUTPUT_FULL_BIT: equ (1 << 0)
PS2_STATUS_INPUT_FULL_BIT:  equ (1 << 1)

; Commands
PS2_CONTROLLER_COMMAND: equ 0xD1

; Controller output port bits
PS2_CONTROLLER_OUTPUT_NO_RESET_BIT:               equ (1 << 0)
PS2_CONTROLLER_OUTPUT_A20_BIT:                    equ (1 << 1)
PS2_CONTROLLER_OUTPUT_PORT2_CLOCK_BIT:            equ (1 << 2)
PS2_CONTROLLER_OUTPUT_PORT2_DATA_BIT:             equ (1 << 3)
PS2_CONTROLLER_OUTPUT_BUFFER_FULL_FROM_PORT1_BIT: equ (1 << 4)
PS2_CONTROLLER_OUTPUT_BUFFER_FULL_FROM_PORT2_BIT: equ (1 << 5)
PS2_CONTROLLER_OUTPUT_PORT1_CLOCK_BIT:            equ (1 << 6)
PS2_CONTROLLER_OUTPUT_PORT1_DATA_BIT:             equ (1 << 7)

PS2_ENABLE_A20_COMMAND: equ \
    PS2_CONTROLLER_OUTPUT_NO_RESET_BIT | \
    PS2_CONTROLLER_OUTPUT_A20_BIT      | \
    PS2_CONTROLLER_OUTPUT_PORT2_CLOCK_BIT | \
    PS2_CONTROLLER_OUTPUT_PORT2_DATA_BIT | \
    PS2_CONTROLLER_OUTPUT_BUFFER_FULL_FROM_PORT1_BIT | \
    PS2_CONTROLLER_OUTPUT_PORT1_CLOCK_BIT | \
    PS2_CONTROLLER_OUTPUT_PORT1_DATA_BIT

; bool drain_8042
drain_8042:
    mov ecx, 10000 ; max attempts
    mov ebx, 16    ; max times we allow 0xFF (potentially not present PS/2)

.drain_once:
    IO_DELAY

    in al, PS2_STATUS_PORT

    cmp al, 0xFF
    jne .test_output

    dec ebx
    jnz .drain_once

    ; controller returned 0xFF too many times
    xor eax, eax
    ret

.test_output:
    test al, PS2_STATUS_INPUT_FULL_BIT
    jz .test_input

.drain_output:
    IO_DELAY
    in al, PS2_DATA_PORT
    jmp .next_iteration

.test_input:
    test al, PS2_STATUS_OUTPUT_FULL_BIT
    jnz .next_iteration

    ; success, both output and input are empty
    mov eax, 1
    ret

.next_iteration:
    dec ecx
    jnz .drain_once

    ; failed to drain after all attempts
    xor eax, eax
    ret

; bool do_enable_a20_using_8042()
; returns true if command was sent successfully
; (doesn't necessarily mean that A20 was enabled)
do_enable_a20_using_8042:
    call drain_8042
    test al, al
    jz .done

    mov al, PS2_CONTROLLER_COMMAND
    out PS2_COMMAND_PORT, al
    call drain_8042

    mov al, PS2_ENABLE_A20_COMMAND
    out PS2_DATA_PORT, al
    call drain_8042

    ; Apparently this is needed for some controllers
    mov al, 0xFF
    out PS2_COMMAND_PORT, al
    call drain_8042

    mov eax, 1

.done:
    ret
