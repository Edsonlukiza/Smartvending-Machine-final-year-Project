from sqlalchemy.orm import Session

from models import Product, Order, OrderItem


def get_products(db: Session):
    return db.query(Product).all()


def get_product(db: Session, product_id: int):
    return (
        db.query(Product)
        .filter(Product.id == product_id)
        .first()
    )


def create_order(
    db: Session,
    order_id: str,
    machine_id: str,
    items
):
    total = 0
    order_items = []

    # Check every requested product first
    for item in items:

        product = get_product(db, item.product_id)

        if not product:
            raise ValueError(
                f"Product {item.product_id} not found"
            )

        if item.quantity <= 0:
            raise ValueError(
                "Quantity must be greater than zero"
            )

        if product.quantity < item.quantity:
            raise ValueError(
                f"Not enough stock for {product.name}"
            )

        item_total = product.price * item.quantity
        total += item_total

        order_items.append(
            OrderItem(
                order_id=order_id,
                product_id=product.id,
                quantity=item.quantity,
                unit_price=product.price
            )
        )

    # Create the order
    order = Order(
        id=order_id,
        machine_id=machine_id,
        status="pending",
        total=total
    )

    db.add(order)

    # Add order items
    for order_item in order_items:
        db.add(order_item)

    db.commit()

    return order