// -*- c-basic-offset: 4; related-file-name: "../include/click/nameinfo.hh" -*-
/*
 * nameinfo.{cc,hh} -- stores name information
 * Eddie Kohler
 *
 * Copyright (c) 2005 The Regents of the University of California
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include <click/nameinfo.hh>
#include <click/glue.hh>
#include <click/confparse.hh>
#include <click/router.hh>
#include <click/error.hh>
CLICK_DECLS

static NameInfo *the_name_info;

void *
StaticNameDB::find(const String &name, bool create)
{
    if (create)
	return 0;
    
    const char *namestr = name.c_str();
    int l = 0, r = _nentries - 1;
    while (l <= r) {
	int m = (l + r) / 2;
	int cmp = strcmp(namestr, _entries[m].name);
	if (cmp == 0)
	    return const_cast<uint32_t *>(&_entries[m].value);
	else if (cmp < 0)
	    r = m - 1;
	else
	    l = m + 1;
    }
    return 0;
}

String
StaticNameDB::revfind(const void *value, int data)
{
    assert(data == 4);
    uint32_t ivalue;
    memcpy(&ivalue, value, 4);
    for (int i = 0; i < _nentries; i++)
	if (_entries[i].value == ivalue)
	    return String::stable_string(_entries[i].name);
    return String();
}


void *
DynamicNameDB::find(const String &name, bool create)
{
    if (_sorted > 20)
	sort();

    if (_sorted == 100) {
	int l = 0, r = _names.size() - 1, m;
	while (l <= r) {
	    m = (l + r) / 2;
	    int cmp = String::compare(name, _names[m]);
	    if (cmp == 0)
		return _values.data() + data() * m;
	    else if (cmp < 0)
		r = m - 1;
	    else
		l = m + 1;
	}
    } else {
	_sorted++;
	for (int i = 0; i < _names.size(); i++)
	    if (name == _names[i])
		return _values.data() + data() * i;
    }

    if (create) {
	_sorted = 0;
	_names.push_back(name);
	_values.extend(data());
	return _values.data() + _values.length() - data();
    } else
	return 0;
}

static int
namelist_sort_compar(const void *athunk, const void *bthunk, void *othunk)
{
    const int *a = (const int *) athunk, *b = (const int *) bthunk;
    const String *o = (const String *) othunk;
    return String::compare(o[*a], o[*b]);
}

void
DynamicNameDB::sort()
{
    if (_sorted == 100)
	return;
    
    Vector<int> permutation(_names.size(), 0);
    for (int i = 0; i < _names.size(); i++)
	permutation[i] = i;
    click_qsort(permutation.begin(), permutation.size(), sizeof(int), namelist_sort_compar, _names.begin());
    
    Vector<String> new_names(_names.size(), String());
    StringAccum new_values(_values.length());
    new_values.extend(_values.length());

    String *nn = new_names.begin();
    char *nv = new_values.data();
    for (int i = 0; i < _names.size(); i++, nn++, nv += data()) {
	*nn = _names[permutation[i]];
	memcpy(nv, _values.data() + data() * permutation[i], data());
    }

    _names.swap(new_names);
    _values.swap(new_values);
    _sorted = 100;
}

String
DynamicNameDB::revfind(const void *value, int data)
{
    const uint8_t *dx = (const uint8_t *) _values.data();
    for (int i = 0; i < _names.size(); i++, dx += data)
	if (memcmp(dx, value, data) == 0)
	    return _names[i];
    return String();
}


NameInfo::NameInfo()
{
#ifdef CLICK_NAMEDB_CHECK
    _check_generation = (uintptr_t) this;
#endif
}

NameInfo::~NameInfo()
{
    for (int i = 0; i < _namedbs.size(); i++)
	delete _namedbs[i];
}

void
NameInfo::static_initialize()
{
    the_name_info = new NameInfo;
}

void
NameInfo::static_cleanup()
{
    delete the_name_info;
}

#if 0
String
NameInfo::NameList::rlookup(uint32_t val)
{
    assert(_data == 4);
    const uint32_t *x = (const uint32_t *) _values.data();
    for (int i = 0; i < _names.size(); i++)
	if (x[i] == val)
	    return _names[i];
    return String();
}
#endif

NameDB *
NameInfo::namedb(uint32_t type, int data, const String &prefix, NameDB *install)
{
    NameDB *db;

    // binary-search types
    int l = 0, r = _namedb_roots.size() - 1, m;
    while (l <= r) {
	m = (l + r) / 2;
	if (type == _namedb_roots[m]->_type)
	    goto found_root;
	else if (type < _namedb_roots[m]->_type)
	    r = m - 1;
	else
	    l = m + 1;
    }

    // type not found
    if (install == install_dynamic_sentinel())
	install = new DynamicNameDB(type, prefix, data);
    if (install) {
	assert(!install->_installed);
	install->_installed = this;
	_namedbs.push_back(install);
	_namedb_roots.insert(_namedb_roots.begin() + l, install);
	return install;
    } else
	return 0;

  found_root:
    // walk tree to find prefix match; keep track of closest prefix
    db = _namedb_roots[m];
    NameDB *closest = 0;
    while (db) {
	if (db->_prefix == prefix) {
	    assert(data < 0 || db->_data == data);
	    return db;
	} else if (db->_prefix.length() < prefix.length()
		   && memcmp(db->_prefix.data(), prefix.data(), db->_prefix.length()) == 0) {
	    closest = db;
	    db = db->_prefix_child;
	} else
	    db = db->_prefix_sibling;
    }

    // prefix not found
    if (install == install_dynamic_sentinel())
	install = new DynamicNameDB(type, prefix, data);
    if (install) {
	assert(!install->_installed);
	install->_installed = this;
	_namedbs.push_back(install);
	install->_prefix_parent = closest;
	NameDB **pp = (closest ? &closest->_prefix_child : &_namedb_roots[m]);
	install->_prefix_sibling = *pp;
	*pp = install;
	// adopt nodes that should be our children
	pp = &install->_prefix_sibling;
	while (*pp) {
	    if (prefix.length() < (*pp)->_prefix.length()
		&& memcmp((*pp)->_prefix.data(), prefix.data(), prefix.length()) == 0) {
		NameDB *new_child = *pp;
		*pp = new_child->_prefix_sibling;
		new_child->_prefix_parent = install;
		new_child->_prefix_sibling = install->_prefix_child;
		install->_prefix_child = new_child;
	    } else
		pp = &(*pp)->_prefix_sibling;
	}
	return install;
    } else {
	assert(!closest || data < 0 || closest->_data == data);
	return closest;
    }
}

NameDB *
NameInfo::getdb(uint32_t type, const Element *e, int data, bool create)
{
    if (e) {
	if (NameInfo *ni = (create ? e->router()->force_name_info() : e->router()->name_info())) {
	    NameDB *install = (create ? ni->install_dynamic_sentinel() : 0);
	    int last_slash = e->id().find_right('/');
	    if (last_slash >= 0)
		return ni->namedb(type, data, e->id().substring(0, last_slash + 1), install);
	    else
		return ni->namedb(type, data, String(), install);
	}
    }

    NameDB *install = (create ? the_name_info->install_dynamic_sentinel() : 0);
    return the_name_info->namedb(type, data, String(), install);
}

void
NameInfo::installdb(NameDB *db, const Element *prefix)
{
    NameInfo *ni = (prefix ? prefix->router()->force_name_info() : the_name_info);
    if (ni->namedb(db->type(), db->data(), db->prefix(), db) != db)
	delete db;
}

void
NameInfo::removedb(NameDB *db)
{
    if (!db->_installed)
	return;
    
    // This is an uncommon operation, so don't worry about its performance.
    NameInfo *ni = db->_installed;
    int m;
    for (m = 0; m < ni->_namedb_roots.size(); m++)
	if (ni->_namedb_roots[m]->_type == db->_type)
	    break;

    NameDB **pp = (db->_prefix_parent ? &db->_prefix_parent->_prefix_child
		   : &ni->_namedb_roots[m]);
    // Remove from sibling list
    for (NameDB *sib = *pp; sib; sib = sib->_prefix_sibling)
	if (sib->_prefix_sibling == db) {
	    sib->_prefix_sibling = db->_prefix_sibling;
	    break;
	}
    // Patch children in
    while (NameDB *cdb = db->_prefix_child) {
	db->_prefix_child = cdb->_prefix_sibling;
	cdb->_prefix_parent = db->_prefix_parent;
	cdb->_prefix_sibling = *pp;
	*pp = cdb;
    }
    // Maybe remove root
    if (!*pp && !db->_prefix_parent)
	ni->_namedb_roots.erase(pp);
    // Remove from _namedbs
    for (int i = 0; i < ni->_namedbs.size(); i++)
	if (ni->_namedbs[i] == db) {
	    ni->_namedbs[i] = ni->_namedbs.back();
	    ni->_namedbs.pop_back();
	    break;
	}
    // Mark as not installed
    db->_installed = 0;
}

bool
NameInfo::query(uint32_t type, const Element *e, const String &name, void *value, int data)
{
    while (1) {
	NameDB *db = getdb(type, e, data, false);
	while (db) {
	    if (db->query(name, value, data))
		return true;
	    db = db->prefix_parent();
	}
	if (!e)
	    return false;
	e = 0;
    }
}

bool
NameInfo::query_int(uint32_t type, const Element *e, const String &name, int32_t *value)
{
    return query(type, e, name, value, 4) || cp_integer(name, value);
}

bool
NameInfo::query_int(uint32_t type, const Element *e, const String &name, uint32_t *value)
{
    return query(type, e, name, value, 4) || cp_unsigned(name, value);
}

String
NameInfo::revquery(uint32_t type, const Element *e, const void *value, int data)
{
    while (1) {
	NameDB *db = getdb(type, e, data, false);
	while (db) {
	    if (String s = db->revfind(value, data))
		return s;
	    db = db->prefix_parent();
	}
	if (!e)
	    return String();
	e = 0;
    }
}


#ifdef CLICK_NAMEDB_CHECK
void
NameInfo::check(ErrorHandler *errh)
{
    StringAccum sa;
    sa << "NameInfo[" << (void*) this << "]: ";
    PrefixErrorHandler perrh(errh, sa.take_string());
    _check_generation++;
    for (int i = 0; i < _namedb_roots.size(); i++) {
	NameDB *db = _namedb_roots[i];
	if (i < _namedb_roots.size() - 1
	    && db->type() >= _namedb_roots[i+1]->type())
	    perrh.error("db roots out of order at %i (%u/%u)", i, (unsigned) db->type(), (unsigned) _namedb_roots[i+1]->type());
	checkdb(db, 0, &perrh);
    }
    for (int i = 0; i < _namedbs.size(); i++)
	if (_namedbs[i]->_check_generation != _check_generation)
	    perrh.error("DB[%u %s %p] in namedbs, but inaccessible", _namedbs[i]->_type, _namedbs[i]->_prefix.c_str(), _namedbs[i]);
}

void
NameInfo::checkdb(NameDB *db, NameDB *parent, ErrorHandler *errh)
{
    StringAccum sa;
    sa << "DB[" << db->_type << ' ';
    if (db->_prefix)
	sa << db->_prefix << ' ';
    sa << (void*) db << "]: ";
    PrefixErrorHandler perrh(errh, sa.take_string());

    // check self
    if (!db->_installed)
	perrh.error("not installed");
    if (db->_installed != this)
	perrh.error("installed in %p, not this NameInfo", db->_installed);
    if (db->_check_generation == _check_generation)
	perrh.error("installed in more than one place");
    db->_check_generation = _check_generation;
    for (int i = 0; i < _namedbs.size(); i++)
	if (_namedbs[i] == db)
	    goto found_in_namedbs;
    perrh.error("not in _namedbs");
  found_in_namedbs:
    
    // check parent relationships
    if (db->_prefix_parent != parent)
	perrh.error("bad parent (%p/%p)", db->_prefix_parent, parent);
    else if (parent && (db->_prefix.length() <= parent->_prefix.length()
			|| db->_prefix.substring(0, parent->_prefix.length()) != parent->_prefix))
	perrh.error("parent prefix (%s) disagrees with prefix", parent->_prefix.c_str());
    if (db->_prefix && db->_prefix.back() != '/')
	perrh.error("prefix doesn't end with '/'");
    if (parent && parent->_type != db->_type)
	perrh.error("parent DB[%u %s %p] has different type", parent->_type, parent->_prefix.c_str(), parent);
    if (parent && parent->_data != db->_data)
	perrh.error("parent DB[%u %s %p] has different data (%u/%u)", parent->_type, parent->_prefix.c_str(), parent, parent->_data, db->_data);
    
    // check sibling relationships
    for (NameDB* sib = db->_prefix_sibling; sib; sib = sib->_prefix_sibling) {
	int l1 = db->_prefix.length(), l2 = sib->_prefix.length();
	if (l1 < l2 ? sib->_prefix.substring(0, l1) == db->_prefix
	    : db->_prefix.substring(0, l2) == sib->_prefix)
	    perrh.error("sibling DB[%u %s %p] should have parent/child relationship", sib->_type, sib->_prefix.c_str(), sib);
	if (sib->_type != db->_type)
	    perrh.error("sibling DB[%u %s %p] has different data", sib->_type, sib->_prefix.c_str(), sib);
	if (sib->_data != db->_data)
	    perrh.error("sibling DB[%u %s %p] has different data (%u/%u)", sib->_type, sib->_prefix.c_str(), sib, sib->_data, db->_data);
    }
    
    // check db itself
    db->check(&perrh);

    // recurse down and to the side
    perrh.message("OK");
    if (db->_prefix_child) {
	PrefixErrorHandler perrh2(errh, "  ");
	checkdb(db->_prefix_child, db, &perrh2);
    }
    if (db->_prefix_sibling)
	checkdb(db->_prefix_sibling, parent, errh);
}

void
StaticNameDB::check(ErrorHandler *errh)
{
    for (int i = 0; i < _nentries - 1; i++)
	if (strcmp(_entries[i].name, _entries[i+1].name) >= 0)
	    errh->error("entries %d/%d (%s/%s) out of order", i, i+1, _entries[i].name, _entries[i+1].name);
}

void
DynamicNameDB::check(ErrorHandler *errh)
{
    if (_sorted == 100)
	for (int i = 0; i < _names.size() - 1; i++)
	    if (String::compare(_names[i], _names[i+1]) >=0)
		errh->error("entries %d/%d (%s/%s) out of order", i, i+1, _names[i].c_str(), _names[i+1].c_str());
    if (_values.length() != _names.size() * data())
	errh->error("odd value length %d (should be %d)", _values.length(), _names.size() * data());
}

void
NameInfo::check(const Element *e, ErrorHandler *errh)
{
    if (e)
	if (NameInfo *ni = e->router()->name_info())
	    ni->check(errh);
    the_name_info->check(errh);
}
#endif


CLICK_ENDDECLS