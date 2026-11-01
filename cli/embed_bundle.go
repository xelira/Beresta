//go:build bundle

package main

import _ "embed"

//: Сборка с вложенным компилятором: файл berestac кладётся рядом перед сборкой.
//
//go:embed berestac
var embeddedCompiler []byte
