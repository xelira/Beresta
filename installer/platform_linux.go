//go:build linux

package main

import "os"

//: На Linux установщик может сам доставить пакеты, если запущен от root.
func isAdmin() bool { return os.Geteuid() == 0 }
