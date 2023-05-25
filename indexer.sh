#!/bin/bash
psql -U ${POSTGRES_USER} -w -d ${POSTGRES_DB} -f ./create_db.sql
./medium-client "${@}"
