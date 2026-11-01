//go:build windows

package main

//: В Windows установщик не ставит системные пакеты напрямую.
func isAdmin() bool { return false }
