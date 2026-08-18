from database import SessionLocal
from models import Product


db = SessionLocal()

products = [
    Product(
        id=1,
        name="Coca Cola",
        price=1500,
        quantity=10
    ),
    Product(
        id=2,
        name="Fanta",
        price=1400,
        quantity=8
    ),
    Product(
        id=3,
        name="Water",
        price=1000,
        quantity=20
    )
]

for product in products:
    existing = db.query(Product).filter(Product.id == product.id).first()

    if not existing:
        db.add(product)

db.commit()
db.close()

print("Products added successfully.")