//go:build !bundle

package main

//: Обычная сборка: компилятор ищется рядом с лаунчером или в PATH.
var embeddedCompiler []byte
