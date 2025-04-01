
-- fat cursor:
-- vim.opt.guicursor = ""

vim.opt.mouse = 'a' -- enable mouse support in all modes
-- vim.opt.clipboard = 'unnamedplus'  -- Use the system clipboard for copy/paste

vim.opt.termguicolors = true  -- Enable 24-bit RGB colors
vim.opt.laststatus = 3  -- Always show the status line

vim.opt.nu = true
vim.opt.relativenumber = true

vim.opt.tabstop = 4
vim.opt.softtabstop = 4
vim.opt.shiftwidth = 4
vim.opt.expandtab = true

vim.opt.smartindent = true

vim.opt.wrap = false

vim.opt.swapfile = false
vim.opt.backup = false
vim.opt.undodir = os.getenv("HOME") .. "/.vim/undodir"
vim.opt.undofile = true

-- highlighting for searches:
vim.opt.hlsearch = true
vim.opt.incsearch = true

vim.opt.termguicolors = true

vim.opt.scrolloff = 8
vim.opt.signcolumn = "yes"
-- vim.opt.isfname:append("@-@")

vim.opt.updatetime = 50

vim.opt.colorcolumn = "110"

vim.g.mapleader = " "

-- vim.opt.autochdir = true

-- setting the language for some file extensions:
vim.filetype.add({
    extension = {
        ixx = 'cpp', -- C++20 modules interface file
        cxx = 'cpp', -- C++20 modules implementation file
    }
})

-- Enable it permanently by entering :set ignorecase or :set ic.
vim.opt.ignorecase = true

-- Optionally, use :set smartcase to make searches case-sensitive only
-- if uppercase letters are included in the search term.
vim.opt.smartcase = true


