<?php
/* Servers Configuration */
$i = 0;

/* Server mariadb (local) */
$i++;
$cfg['Servers'][$i]['verbose'] = 'MariaDB';
$cfg['Servers'][$i]['host'] = 'mariadb';
$cfg['Servers'][$i]['port'] = '';
$cfg['Servers'][$i]['socket'] = '';
$cfg['Servers'][$i]['connect_type'] = 'tcp';
$cfg['Servers'][$i]['extension'] = 'mysqli';
$cfg['Servers'][$i]['auth_type'] = 'config';
$cfg['Servers'][$i]['user'] = 'root';
$cfg['Servers'][$i]['password'] = 'toor';

/* Server postgres (local) */
$i++;
$cfg['Servers'][$i]['verbose'] = 'PostgreSQL';
$cfg['Servers'][$i]['host'] = 'postgres';
$cfg['Servers'][$i]['port'] = '';
$cfg['Servers'][$i]['socket'] = '';
$cfg['Servers'][$i]['connect_type'] = 'tcp';
$cfg['Servers'][$i]['extension'] = 'pgsql';
$cfg['Servers'][$i]['auth_type'] = 'config';
$cfg['Servers'][$i]['user'] = 'root';
$cfg['Servers'][$i]['password'] = 'toor';

/* Server supabase-db (local) */
$i++;
$cfg['Servers'][$i]['verbose'] = 'Supabase PostgreSQL';
$cfg['Servers'][$i]['host'] = 'supabase-db';
$cfg['Servers'][$i]['port'] = '';
$cfg['Servers'][$i]['socket'] = '';
$cfg['Servers'][$i]['connect_type'] = 'tcp';
$cfg['Servers'][$i]['extension'] = 'pgsql';
$cfg['Servers'][$i]['auth_type'] = 'config';
$cfg['Servers'][$i]['user'] = 'supabase_admin';
$cfg['Servers'][$i]['password'] = 'toor';

/* End of servers configuration */
?>