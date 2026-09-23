Features that __will not__ be added to `ngx_cache_purge`:

* Support for wildcard/regex purges (`/purge/*.jpg`).  
  Reason: Impossible with current cache implementation.

Note: prefixed purges (`/purge/images/*`) are supported via the
`*_cache_purge_prefix` directives (see README).
