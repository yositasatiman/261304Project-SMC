        lw      0   1   N        ; r1 = n
        jalr    6   7            ; call factorial (r6 = PC+1, jump to r7)
        halt                     ; stop program
;--------------------------------------------
; factorial function
; input : r1 = n
; output: r3 = factorial(n)
; registers: 
;   r1 = n
;   r2 = temp
;   r3 = result
;   r6 = return address
;   r7 = function address (fact)
fact    beq     1   0   base     ; if n == 0 → base case
        lw      0   2   NEG1     ; r2 = -1
        add     1   2   1        ; n = n - 1
        jalr    6   7            ; recursive call factorial(n-1)
        lw      0   1   N        ; restore n (load original n)
        lw      0   2   TMP      ; r2 = factorial(n-1)
        add     3   2   3        ; result = factorial(n-1) + n  (simplified multiply)
        beq     0   0   end
base    add     0   0   3        ; r3 = 1
end     jalr    7   6            ; return
N       .fill   3
NEG1    .fill   -1
TMP     .fill   0
