"""The tables written by cloud/subscriber.py (see cloud/schema.sql).

managed = False: Django only reads them, it never creates or migrates them.
"""
from django.db import models


class Parcel(models.Model):
    session = models.CharField(max_length=32)
    seq = models.IntegerField()
    parcel_id = models.CharField(max_length=32, null=True)
    block = models.CharField(max_length=8, null=True)
    floor = models.SmallIntegerField(null=True)
    unit = models.CharField(max_length=8, null=True)
    scan = models.CharField(max_length=16, null=True)
    flips = models.SmallIntegerField(null=True)
    zone = models.CharField(max_length=8, null=True)
    routed = models.CharField(max_length=8, null=True)
    reason = models.CharField(max_length=16, null=True)
    result = models.CharField(max_length=16, null=True)
    arrived_at = models.DateTimeField(null=True)
    scanned_at = models.DateTimeField(null=True)
    sorted_at = models.DateTimeField(null=True)

    class Meta:
        managed = False
        db_table = "parcels"


class Fault(models.Model):
    session = models.CharField(max_length=32, null=True)
    source = models.CharField(max_length=16)
    type = models.CharField(max_length=32)
    seq = models.IntegerField(null=True)
    detail = models.CharField(max_length=255, null=True)
    at = models.DateTimeField()

    class Meta:
        managed = False
        db_table = "faults"
