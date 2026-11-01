" Подсветка синтаксиса бересты для Vim.
" Установка: скопировать каталог editors/vim/* в ~/.vim/
" или подключить через менеджер плагинов.

if exists("b:current_syntax")
  finish
endif

syntax keyword berestaKeyword слово устой дело уклад береста ежели инако доколе повтор от до шаг для каждого из вернуть прервать продолжить молви
syntax keyword berestaType число дробь строка флаг ничто
syntax keyword berestaBoolean правда кривда
syntax keyword berestaOperator и или не
syntax keyword berestaBuiltin длина к_строке из_строки внемли слушает патриот пишет в_число в_дробь мова

syntax match berestaComment "//.*$"
syntax region berestaComment start="/\*" end="\*/"

syntax region berestaString start=+"+ skip=+\\"+ end=+"+ contains=berestaEscape
syntax match berestaEscape "\\[ntr0\\\"]" contained

syntax match berestaNumber "\<\d[\d_]*\(\.\d[\d_]*\)\?\>"
syntax match berestaFunction "\<[[:alpha:]_][[:alnum:]_]*\>\_s*(" contains=berestaKeyword,berestaType

highlight default link berestaKeyword Keyword
highlight default link berestaType Type
highlight default link berestaBoolean Boolean
highlight default link berestaOperator Operator
highlight default link berestaComment Comment
highlight default link berestaString String
highlight default link berestaEscape SpecialChar
highlight default link berestaNumber Number
highlight default link berestaFunction Function
highlight default link berestaBuiltin Function

setlocal iskeyword+=а-яА-ЯёЁ

let b:current_syntax = "beresta"
