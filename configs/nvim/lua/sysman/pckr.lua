local function bootstrap_pckr()
  local pckr_path = vim.fn.stdpath("data") .. "/pckr/pckr.nvim"

  if not (vim.uv or vim.loop).fs_stat(pckr_path) then
    vim.fn.system({
      'git',
      'clone',
      "--filter=blob:none",
      'https://github.com/lewis6991/pckr.nvim',
      pckr_path
    })
  end

  vim.opt.rtp:prepend(pckr_path)
end

bootstrap_pckr()

require('pckr').add{

  {'nvim-treesitter/nvim-treesitter', run = ':TSUpdate' };

  {'nvim-telescope/telescope.nvim',
    -- tag = '0.1.8',
    requires = {'nvim-lua/plenary.nvim'}
  };


  -- rose-pine theme
  {
    'rose-pine/neovim',
    as = 'rose-pine',
    config = function()
      vim.cmd("colorscheme rose-pine")
    end
  };

  -- tokyo night theme
  'folke/tokyonight.nvim';

  -- catppuccin theme
  {'catppuccin/nvim', as = 'catppuccin'};

  -- gruvbox theme
  {'sainnhe/gruvbox-material', as = 'gruvbox'};

  -- nightfox theme
  'EdenEast/nightfox.nvim';

  -- undo tree
  'mbbill/undotree';


  -- Git
  'tpope/vim-fugitive';
  'lewis6991/gitsigns.nvim';

  -- lsp
  {
    'VonHeikemen/lsp-zero.nvim',
    -- branch = 'v4.x'
  };
  {'neovim/nvim-lspconfig'};
  {'hrsh7th/cmp-nvim-lsp'};
  {'hrsh7th/nvim-cmp'};

  -- File explorer
  {
    "nvim-neo-tree/neo-tree.nvim",
    branch = "v3.x",
    dependencies = {
      "nvim-lua/plenary.nvim",
      "nvim-tree/nvim-web-devicons",
      "MunifTanjim/nui.nvim",
    },
  };

  -- Formatting
  {
    'stevearc/conform.nvim',
    config = function()
      require('conform').setup({
        formatters_by_ft = {
          lua = { 'stylua' },
          python = { 'black' },
          javascript = { 'prettierd' },
          typescript = { 'prettierd' },
          json = { 'prettierd' },
          yaml = { 'prettierd' },
          markdown = { 'prettierd' },
          c = { 'clang-format' },
          cpp = { 'clang-format' },
          rust = { 'rustfmt' },
          sh = { 'shfmt' },
        },
        format_on_save = {
          timeout_ms = 500,
          lsp_format = 'fallback',
        },
      })
      vim.keymap.set("n", "<leader>cf", function()
        require('conform').format({ async = true, lsp_format = 'fallback' })
      end, { desc = "Format file" })
    end,
  };

  -- Diagnostics / Quickfix
  {
    "folke/trouble.nvim",
    dependencies = { "nvim-tree/nvim-web-devicons" },
    config = function()
      require('trouble').setup({})
      vim.keymap.set("n", "<leader>xx", "<cmd>Trouble diagnostics toggle<cr>", { desc = "Diagnostics (Trouble)" })
      vim.keymap.set("n", "<leader>xb", "<cmd>Trouble diagnostics toggle filter.buf=0<cr>", { desc = "Buffer Diagnostics (Trouble)" })
      vim.keymap.set("n", "<leader>q", "<cmd>Trouble qflist toggle<cr>", { desc = "Quickfix List (Trouble)" })
    end,
  };

  -- scrollbar:
  'lewis6991/satellite.nvim';
  -- petertriho/nvim-scrollbar
  -- dstein64/nvim-scrollview

  -- icons:
  'nvim-tree/nvim-web-devicons';

  -- indent blankline
  'lukas-reineke/indent-blankline.nvim';

  -- todo comments
  {'folke/todo-comments.nvim', requires = { 'nvim-lua/plenary.nvim' }};

  -- tabs
  'romgrk/barbar.nvim';

  -- which keys
  'folke/which-key.nvim';

  -- status line
  'nvim-lualine/lualine.nvim';

  -- notify
  'rcarriga/nvim-notify';

  -- multi-cursor
  {'mg979/vim-visual-multi', branch = 'master'};

  -- smooth scrolling
  'declancm/cinnamon.nvim';
}

-- You can find more plugins here:
--   https://github.com/rockerBOO/awesome-neovim?tab=readme-ov-file
--   https://dotfyle.com/neovim/plugins/top

