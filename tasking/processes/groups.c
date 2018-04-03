/*  
    This file is part of VK.
    Copyright (C) 2018 Valentin Haudiquet

    VK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    VK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VK.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "tasking/task.h"

//groups is a sorted array of 'pgroup_t' acting as process group table
pgroup_t* groups = 0;
u32 groups_number = 0;
u32 groups_size = 0;

static pgroup_t* process_addgroup(int gid, process_t* process);

/*
* Binary search on groups sorted array
*/
pgroup_t* get_group(int gid)
{
    u32 start = 0, end = groups_number;
    u32 off = 0;

    while(start <= end)
    {
        off = ((start+end)/2);
        if(groups[off].gid == gid) return &groups[off];
        else if(groups[off].gid < gid) start = off+1;
        else if(groups[off].gid > gid) end = off-1;
    }

    return 0;
}

/*
* Remove 'process' from his group and set it to the new group
*/
error_t process_setgroup(int gid, process_t* process)
{
    //TODO : check if process can join, remove from his origin group, ..
    process_addgroup(gid, process);
    return ERROR_NONE;
}

/*
* Add 'process' to group 'gid', creating it if it doesnt exist
*/
static pgroup_t* process_addgroup(int gid, process_t* process)
{
    u32 start = 0, end = groups_number;
    u32 off = 0;

    while(start <= end)
    {
        off = ((start+end)/2);
        if(groups[off].gid == gid) 
        {
            //adding process to the list
            list_entry_t** ptr = &groups[off].processes;
            while(*ptr) ptr = &((*ptr)->next);
            (*ptr) = kmalloc(sizeof(list_entry_t));
            (*ptr)->next = 0;
            (*ptr)->element = process;

            return &groups[off];
        }
        else if(groups[off].gid < gid) start = off+1;
        else if(groups[off].gid > gid) end = off-1;
    }

    if(groups_number == groups_size) {groups_size*=2; groups = krealloc(groups, groups_size*sizeof(pgroup_t));}

    memcpy(&groups[off+1], &groups[off], (groups_number-off)*sizeof(pgroup_t));
    groups_number++;
    groups[off].gid = gid;
    groups[off].processes = kmalloc(sizeof(list_entry_t));
    groups[off].processes->element = process;
    groups[off].processes->next = 0;

    return &groups[off];
}
