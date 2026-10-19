"""Safety boundaries: a review/cache must not silently follow another cut."""
import copy,unittest
from runner import validate_review,load,HERE

class ReviewContract(unittest.TestCase):
    def setUp(self):
        self.c=load(HERE/'review-176-v1.json')
        self.s={k:self.c[k] for k in ['width','height','frames','fps']}
    def validate(self):validate_review(self.c,self.s,self.c['sourceSha256'])
    def test_valid(self):self.validate()
    def test_source_changed(self):
        with self.assertRaises(ValueError):validate_review(self.c,self.s,'wrong-cut')
    def test_dimensions_changed(self):
        self.s['width']=1440
        with self.assertRaises(ValueError):self.validate()
    def test_unreviewed(self):
        self.c['reviewed']=False
        with self.assertRaises(ValueError):self.validate()
    def test_cuts_not_sorted(self):
        self.c['cuts']=[0,50,30]
        with self.assertRaises(ValueError):self.validate()
    def test_cut_outside(self):
        self.c['cuts']=[0,self.s['frames']]
        with self.assertRaises(ValueError):self.validate()
    def test_rectangle_outside(self):
        self.c['rectangles'][0]['box'][2]=5000
        with self.assertRaises(ValueError):self.validate()
    def test_override_without_corridor(self):
        self.c['reviewedHandOverrides']=[[0,50]]
        with self.assertRaises(ValueError):self.validate()
    def test_bypass_invalid(self):
        self.c['fullBypassIntervals']=[[30,10]]
        with self.assertRaises(ValueError):self.validate()
    def test_zero_fade(self):
        self.c['fadeFrames']=0
        with self.assertRaises(ValueError):self.validate()

if __name__=='__main__':unittest.main()
