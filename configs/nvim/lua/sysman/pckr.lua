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
    tag = '0.1.8',
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

  -- undo tree
  'mbbill/undotree';


  -- Git
  'tpope/vim-fugitive';
  'lewis6991/gitsigns.nvim';

  -- lsp
  {'VonHeikemen/lsp-zero.nvim', branch = 'v4.x'};
  {'neovim/nvim-lspconfig'};
  {'hrsh7th/cmp-nvim-lsp'};
  {'hrsh7th/nvim-cmp'};

  -- NerdTree
  'preservim/nerdtree';
  'ryanoasis/vim-devicons'; -- icons

  -- scrollbar:
  'lewis6991/satellite.nvim';
  -- petertriho/nvim-scrollbar
  -- dstein64/nvim-scrollview

  -- icons:
  'nvim-tree/nvim-web-devicons';

  -- indent blankline
  'lukas-reineke/indent-blankline.nvim';

  -- comments
  'numToStr/Comment.nvim';

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
}

-- You can find more plugins here:
--   https://github.com/rockerBOO/awesome-neovim?tab=readme-ov-file
--   https://dotfyle.com/neovim/plugins/top

