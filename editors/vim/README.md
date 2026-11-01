# Vim

Подсветка синтаксиса бересты.

## Установка

Скопировать содержимое в каталог конфигурации Vim:

```sh
mkdir -p ~/.vim/syntax ~/.vim/ftdetect
cp editors/vim/syntax/beresta.vim     ~/.vim/syntax/
cp editors/vim/ftdetect/beresta.vim   ~/.vim/ftdetect/
```

Для Neovim — то же самое, но в `~/.config/nvim/`:

```sh
mkdir -p ~/.config/nvim/syntax ~/.config/nvim/ftdetect
cp editors/vim/syntax/beresta.vim     ~/.config/nvim/syntax/
cp editors/vim/ftdetect/beresta.vim   ~/.config/nvim/ftdetect/
```

Файлы с расширением `.бер` определяются автоматически.
