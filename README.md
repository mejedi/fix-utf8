fix-utf8
========
Sanitizing UTF-8 string in C++; valid portions are transfered as-is while
invalid ones are encoded in UTF-8B.

The implementation is reasonably correct (a few tests exist) and tuned for
performance (TODO not tuned yet).

For UTF-8B, see http://permalink.gmane.org/gmane.comp.internationalization.linux/920
