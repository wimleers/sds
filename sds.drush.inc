<?php

/**
 * @file
 *   SDS (sql-dump-smart) drush command.
 */

/**
 * Implementation of hook_drush_command().
 *
 * @return
 *   An associative array describing your command(s).
 */
function sds_drush_command() {
  $options['--database'] = 'The DB connection key if using multiple connections in settings.php.';
  if (drush_drupal_major_version() >= 7) {
    $options['--target'] = 'The name of a target within the specified database.';
  }

  $items = array();

  $items['sql-dump-smart'] = array(
    'description' => 'Dumps all the databases of a Drupal installation in a smart manner: only structure and no data for "cache", "sessions" and similar tables is dumped. Alternative to the core sql-dump command, with support for table name expansion and validation. Based on dgb.',
    'callback' => 'drush_sql_dump_smart',
    // We require to be located in a fully functionnal Drupal site for now
    // until we provide support/help for the case where the user is calling
    // sds outside a Drupal site context.
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_CONFIGURATION,
    'options' => array(
      '--display-dump-command' => 'Display the query used for the dump. Helps to ensure the right tables have been backed up.',
      '--skip-tables-key' => 'A key in the $skip_tables array. @see example.drushrc.php. Optional.',
      '--structure-tables-key' => 'A key in the $structure_tables array. @see example.drushrc.php. Optional.',
      '--tables' => 'An explcit list of tables to dump, in comma delimited format',
      '--ordered-dump' => 'Use this option to output ordered INSERT statements in the sql-dump. Useful when backups are managed in a Version Control System. Optional.',
      '--path' => 'Define the path where the dump will be saved to.',
      '--bzip2' => 'When this option is set, the dump will be bzipped.',
    ) + $options,
  );

  return $items;
}

/**
 * Command callback. Perform a smart SQL dump.
 */
function drush_sql_dump_smart() {
  $sds_settings = _sds_get_required_settings();

  // Set a default list of structure tables which should work for most sites.
  $default_structure_tables = array(
    'common' => array('accesslog', 'cache', 'cache_*', 'history', 'search_dataset', 'search_index', 'search_total', 'sessions', 'watchdog'),
  );
  drush_set_default('structure-tables', $default_structure_tables);

  // Find all sites present in this Drupal installation.
  $sites = drush_scan_directory('sites', '/.*/', array('.', '..', 'CVS'), 0, $recurse = FALSE);

  // Ignores the symbolic links. These are not supported by sds, mainly because
  // we're not sure yet how git should handle them. Git only backs up files
  // which are physically stored within the Drupal installation directory.
  foreach ($sites as $site) {
    if (is_link($site->filename)) {
      drush_print($site->filename . " is a symbolic link and the database defined in its settings.php will not be backed up.\n");
      continue;
    }
    // Ignore directories without a settings.php
    if (!is_file($site->filename . '/settings.php')) {
      continue;
    }

    // Each database dump is stored in its own file in the dumps directory.
    drush_set_option('result-file', $sds_settings['path'] . '/' . $site->basename . '.sql');

    // Prepare SQL dump command.
    drush_log('Processing ' . $site->filename);
    $exec = drush_sds_build_dump_command(_sds_sql_get_spec_from_settings($site->filename . '/settings.php', ''));
    if (drush_get_option('display-dump-command')) {
      drush_print('Dump command: ' . $exec . "\n");
    }

    // Avoid the php memory of the $output array in drush_shell_exec().
    drush_op('system', $exec);
  }

  return;
}

function drush_sds_build_dump_command($db_spec = NULL) {
  $ignores = array();

  if (is_null($db_spec)) {
    $db_spec = _drush_sql_get_db_spec();
  }
  $database = $db_spec['database'];

  // Skip large core tables if instructed.  Also used by 'sql-sync' command.
  $skip_tables = _drush_sds_get_table_list($db_spec, 'skip-tables');
  // Skip any structure-tables as well.
  $structure_tables = _drush_sds_get_table_list($db_spec, 'structure-tables');
  $skip_tables += $structure_tables;
  // Dump only the specified tables. Takes precedence over skip-tables and
  // structure-tables.
  $tables = _drush_sds_get_table_list($db_spec, 'tables');
  $data_only = drush_get_option('data-only');
  // The ordered-dump option is only supported by MySQL for now.
  $ordered_dump = drush_get_option('ordered-dump');

  switch (_drush_sql_get_scheme($db_spec)) {
    case 'mysql':
      $exec = 'mysqldump' . (drush_get_context('DRUSH_VERBOSE') ? ' -v' : '');
      if ($file = drush_get_option('result-file')) {
        $exec .= ' --result-file '. $file;
      }
      $extra = ' --single-transaction --opt -Q' . _drush_sql_get_credentials($db_spec);
      if (isset($data_only)) {
        $extra .= ' --no-create-info';
      }
      if (isset($ordered_dump)) {
       $extra .= ' --skip-extended-insert --order-by-primary';
      }
      $exec .= $extra;

      if (!empty($tables)) {
        $exec .= ' ' . implode(' ', $tables);
      }
      else {
        // Append the ignore-table options.
        foreach ($skip_tables as $table) {
          $ignores[] = "--ignore-table=$database.$table";
        }
        $exec .= ' '. implode(' ', $ignores);

        // Run mysqldump again and append output if we need some structure only
        // tables.
        if (!empty($structure_tables)) {
          $exec .= "; mysqldump --no-data $extra " . implode(' ', $structure_tables);
          if ($file) {
            $exec .= " >> $file";
          }
        }
      }
      break;
    case 'pgsql':
      $create_db = drush_get_option('create-db');
      $exec = 'pg_dump ' . (drush_get_context('DRUSH_VERBOSE') ? ' -v' : '');
      if ($file = drush_get_option('result-file')) {
        $exec .= ' --file '. $file;
      }
      // Unlike psql, pg_dump does not take a '-d' flag before the database
      // name. We'll put the database name in 'by hand' and then clear the name
      // from the record to prevent _drush_sql_get_credentials from inserting
      // a -d term that pg_dump does not understand.
      $extra = ' ' . $db_spec['database'];
      $db_spec['database'] = null;
      $extra .= _drush_sql_get_credentials($db_spec);
      if (isset($data_only)) {
        $extra .= ' --data-only';
      }
      $exec .= $extra;
      $exec .= (!isset($create_db) && !isset($data_only) ? ' --clean' : '');

      if (!empty($tables)) {
        foreach ($tables as $table) {
          $exec .= " --table=$table";
        }
      }
      else {
        foreach ($skip_tables as $table) {
          $ignores[] = "--exclude-table=$table";
        }
        $exec .= ' '. implode(' ', $ignores);
        // Run pg_dump again and append output if we need some structure only
        // tables.
        if (!empty($structure_tables)) {
          $schemaonlies = array();
          foreach ($structure_tables as $table) {
            $schemaonlies[] = "--table=$table";
          }
          $exec .= "; pg_dump --schema-only " . implode(' ', $schemaonlies) . $extra;
          if ($file) {
            $exec .= " >> $file";
          }
        }
      }
      break;
  }

  if (drush_get_option('bzip2')) {
    // Overwrite existing dump and use less memory (at most 2500K).
    $exec .= "; bzip2 --compress --force --small $file";
  }

  return $exec;
}

// Given an option name (skip-tables, structure-tables or tables),
// look up the appropriate list of tables in the given category.
function _drush_sds_get_table_list($db_spec, $option_name) {
  $tables = array();
  foreach(array('', 'source-', 'target-') as $prefix) {
    $key_list = drush_get_option($prefix . $option_name . '-key');
    foreach(explode(',', $key_list) as $key) {
      $all_tables = drush_get_option($option_name, array());
      if (array_key_exists($key, $all_tables)) {
        $tables += $all_tables[$key];
      }
      if ($option_name != 'tables') {
        $all_tables = drush_get_option('tables', array());
        if (array_key_exists($key, $all_tables)) {
          $tables += $all_tables[$key];
        }
      }
    }
    $table_list = drush_get_option($prefix . $option_name . '-list');
    if (isset($table_list)) {
      $tables += explode(',', $table_list);
    }
  }

  // Get the existing table names contained in the current database.
  $db_tables = _drush_sds_get_db_table_list($db_spec);
  // Table name expansion based on * wildcard.
  $expanded_db_tables = array();
  foreach ($tables as $k => $table) {
    // Only deal with table names containing a wildcard.
    if (strpos($table, '*') !== FALSE) {
      $pattern = '/^' . str_replace('*', '.*', $table) . '$/i';
      // Merge those existing tables which match the pattern with the rest of
      // the expanded table names.
      $expanded_db_tables += preg_grep($pattern, $db_tables);
      // Drop the wildcard table, this is not a valid table name.
      unset($tables[$k]);
    }
  }

  // Ensure all the tables actually exist in the database.
  foreach ($tables as $k => $table) {
    if (!in_array($table, $db_tables)) {
      unset($tables[$k]);
    }
  }

  // Merge expanded and validated table names.
  $tables += $expanded_db_tables;

  // Make sure each table name is unique and sort them.
  $tables = array_unique($tables);
  sort($tables);

  return $tables;
}

/**
 * Extract the name of all existing tables in the current local database.
 *
 * @return array
 *   A list of table names which exist in the current database.
 */
function _drush_sds_get_db_table_list($db_spec) {
  $db_tables = array();

  $scheme = _drush_sql_get_scheme($db_spec);

  $query = 'show tables;';
  if ($scheme == 'pgsql') {
    $query = "select tablename from pg_tables where schemaname='public';";
  }

  // We do not have access to the database other than by running the appropriate
  // command line, sending the results into a file and reading it.
  if ($query_file = drush_save_data_to_temp_file($query)) {
    // Create a temporary file for storing the results.
    $results_file = tempnam(sys_get_temp_dir(), 'results.sql.');

    switch ($scheme) {
      case 'mysql':
        $exec = 'mysql' . (drush_get_context('DRUSH_VERBOSE') ? ' -v' : '');
        $exec .= _drush_sql_get_credentials($db_spec);
        $exec .= ' ' . drush_get_option('extra');
        $exec .= " < $query_file > $results_file";

        break;
      case 'pgsql':
        $exec = 'psql';
        $exec .= _drush_sql_get_credentials($db_spec);
        $exec .= (drush_get_context('DRUSH_VERBOSE') ? '' : ' -q');
        $exec .= ' ' . (drush_get_option('extra') ? drush_get_option('extra') : "--no-align --field-separator=$'\t' --pset footer=off");
        $exec .= " --file $query_file > $results_file";
        break;
    }

    // Execute the show tables; command.
    drush_op('system', $exec);
    // Read all the results back in memory.
    $results = file($results_file);
    // We can scratch the query and results files now.
    drush_op('unlink', $query_file);
    drush_op('unlink', $results_file);

    // The first result is the column name.
    unset($results[0]);
    // Clean up table names.
    foreach ($results as &$v) {
      $v = trim($v);
    }
    $db_tables = $results;
  }

  return $db_tables;
}

function _sds_get_required_settings() {
  $sds_settings = array();
  // Ensure the path for the database dumps is given in the options and exists
  // on the filesystem.
  if (!$sds_settings['path'] = drush_get_option('path')) {
    drush_die('sql-dump-smart requires to have the path of the database dumps specified either in the command line or in a drushrc.php file. See README.txt and example-sql.dump.smart.drushrc.php');
  }
  if (!is_dir($sds_settings['path'])) {
    drush_die('The path given in the location option must exist on your filesystem. You must create it yourself.');
  }

  // Default the 'structure-tables-key' option to 'common'.
  if (drush_get_option('structure-tables-key') == NULL) {
    drush_set_option('structure-tables-key', 'common');
  }

  return $sds_settings;
}

// Build a $db_spec from a given settings.php.
// This function was removed from Drush, see http://drupal.org/node/483502
// @todo reintroduce such this function as API function in Drush.
function _sds_sql_get_spec_from_settings($file, $prefix) {
  // Reset global databases to whatever is defined in $file
  global $databases, $db_url;
  require $file;

  // Translate the custom options for sql-sync into ones usually recognized (i.e. no prefix).
  if ($database = drush_get_option($prefix . 'database')) {
    drush_set_option('database', $database);
  }
  if ($target = drush_get_option($prefix . 'target')) {
    drush_set_option('target', $target);
  }
  // Backwards-compatible for sql load:  accept 'source_database' in addition to the more standard 'source-database'
  if ($database = drush_get_option(str_replace('-', '_', $prefix . 'database'))) {
    drush_set_option('database', $value);
  }
  if ($target = drush_get_option(str_replace('-', '_', $prefix . 'target'))) {
    drush_set_option('target', $target);
  }

  return _drush_sql_get_db_spec();
}
