sql-dump-smart (SDS)
=======================

SDS is a Drush command to easily dump Drupal databases in a smart manner; only
dumping the structure of tables with temporary data such as "cache" (and all
"cache_*" tables, such as "cache_page", "cache_views", etc. — a wild card is
used to detect *all* cache tables!), to keep the size of database back-ups to
a minimum.
It supports multi site setups and will dump each site database (optionally as
a version control friendly SQL file).
This Drush command is still at an experimental state so use with caution. Please
give any feedback or file bugs at http://github.com/wimleers/sds

SDS requires Drush 3 <http://drupal.org/project/drush> and works for any
version of Drupal: 5, 6 and 7.


Installation
============

Install SDS like any other Drush command. See Drush's README.txt file. 


Commands
========

SDS ships with a single command:

- sql-dump-smart: Dumps all the databases of a Drupal installation in a smart
                  manner: only structure and no data for "cache", "sessions"
                  and similar tables is dumped. Alternative to the core
                  sql-dump command, with support for table name expansion and
                  validation. Based on DGB.


Author
======

Wim Leers <work@wimleers.com>

Based on DGB by:
Stéphane "scor" Corlosquet <scorlosquet@gmail.com>


References
==========

This drush command uses a similar approach to the one used in dbscripts
<http://drupal.org/project/dbscripts> except it is fully integrated in drush as
opposed to using custom bash scripts.
