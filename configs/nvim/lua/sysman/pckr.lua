local function bootstrap_pckr()
  local pckr_path = vim.fn.stdpath("data") .. "/pckr/pckr.nvim"

  if not vim.uv.fs_stat(pckr_path) then
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

  -- lsp
  {'VonHeikemen/lsp-zero.nvim', branch = 'v4.x'};
  {'neovim/nvim-lspconfig'};
  {'hrsh7th/cmp-nvim-lsp'};
  {'hrsh7th/nvim-cmp'};
}

